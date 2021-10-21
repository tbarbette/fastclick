/*
 * httpin.{cc,hh} -- entry point of an HTTP path in the stack of the middlebox
 * Romain Gaillard
 * Tom Barbette
 *
 * Copyright - University of Liege
 *
 */
#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>

#include "httpin.hh"


CLICK_DECLS


HTTPIn::HTTPIn() : _set10(false), _remove_encoding(false), _buffer(65536), _verbose(false), _resize(false)
{
}

int HTTPIn::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String method = "unset";
    if(Args(conf, this, errh)
        .read("HTTP10", _set10)
        .read("NOENC", _remove_encoding)
        .read("BUFFER", _buffer)
        .read("RESIZE_METHOD", method)
        .read("VERBOSE", _verbose)
        .complete() < 0)
        return -1;

    if (_set10 || _remove_encoding)
        _resize = true;

    if (_buffer > 0 && method != "unset") {
        return errh->error("When buffering, we do not need a resize method. Content-length will be stripped");
    }

    if (_set10) {
        if (method != "unset")
            return errh->error("Filling method cannot be set with HTTP10 (as HTTP10 will remove content length)");
        _fill = RESIZE_HTTP10;
    } else if (method == "fill_end" || method == "unset") {
        click_chatter("HTTPIn will fill the last packet with space when some content is removed");
        _fill = RESIZE_FILL_END;
    } else if (method == "fill") {
        click_chatter("HTTPIn will fill the packet with space when content is removed");
        _fill = RESIZE_FILL;
    } else {
        //TODO : implement method RESIZE_CHUNKED
        return errh->error("Unknown RESIZE_METHOD");
    }

    //TODO : One option to look for would be to remove Accept-Ranges, or keep it only for the server side view of the connection as it may allow to evict an IDS (simply split the fetch in the middle of an attack)
    return 0;
}

void
HTTPIn::requestTerminated() {
    auto fcb = fcb_data();
    fcb->headerFound = false;
    fcb->contentSeen = 0;
    fcb->contentRemoved = 0;
}

int
HTTPIn::maxModificationLevel(Element* stop)
{
        int r = CTXSpaceElement<fcb_httpin>::maxModificationLevel(stop);
        if (_set10 || _remove_encoding)
            return r | MODIFICATION_RESIZE;
}

int constexpr length(const char* str)
{
    return *str ? 1 + length(str + 1) : 0;
}

void HTTPIn::push_flow(int port, fcb_httpin* fcb, PacketBatch* flow)
{
    auto fnt = [this,fcb](Packet* &p) -> bool {
        // Check that the packet contains HTTP content
        if(p->isPacketContentEmpty())
            return true;

        // By default, the packet is not considered to be the last one
        // containing HTTP content
        setAnnotationLastUseful(p, false);

        WritablePacket* packet = p->uniqueify();
        p = packet;
        if(!fcb->headerFound)
        {
            // Search the end of the first line
            char* current = (char*)memchr(packet->getPacketContent(), '\r', getPayloadLength(packet));
            if(current == NULL) {
                    click_chatter("Probable attack : no method in HTTP");
                    closeConnection(packet, false); //Todo : support inter-packet header
                    return false;
            }

            int endOfMethod = current - (char*)packet->getPacketContent();

            if (_set10) {
                packet = setHTTP10(fcb, packet, endOfMethod);
                current = (char*)(packet->getPacketContent() + endOfMethod);
            }
            current += 2;
            StringRef method((char*)packet->getPacketContent(), (char*)current);
            //click_chatter("Method : %s", method.c_str());

//            fcb->isRequest = true;
            // Compute the offset of the HTML payload
            //Hashmap<Header> headers;

            int left = packet->getPacketContentSize() - endOfMethod;
            do {
                char* end = (char*)memchr(current, '\r',left);
                if (end == 0) {
                    click_chatter("Probable attack : no end of header in HTTP");
                    closeConnection(packet, false); //Todo : support inter-packet header
                    return false;
                }
                if (end-current == 0) {
                    break; //Found the double termination
                }
                //Header header;
                char* split = (char*)memchr(current, ':', end-current);
                if (split == NULL) {
                    click_chatter("Malformed HTTP header %s",String(current,end-current).c_str());
                    closeConnection(packet, false);
                    return false;
                }
                StringRef header = StringRef(current, split);
                StringRef value = StringRef(split + 2, end);
                int pos = current - (char*)packet->getPacketContent();

                bool remove = false;
                if (_remove_encoding && header == "Accept-Encoding") {
                    remove = true;
                } else if (header == "Content-Length") {
                    fcb->contentLength = (uint64_t)atol(value.data());
                    if (_buffer == 0 && _resize) {
                        if (_fill == RESIZE_CHUNKED || _set10) {
                            fcb->CLRemoved = true;
                            remove = true;
                            //TODO : add chunked encoding
                        } else { //RESIZE_FILL or FILL_END

                        }
                    }
                } else if (header == "Connection") {
                    if ((_fill == RESIZE_CHUNKED || _set10) && (_buffer == 0 && _resize)) {
                        if (value == "keep-alive") {
                            remove = true;
                            fcb->KARemoved = true;
                        }
                    }
                }

                //click_chatter("header %s, value %s", header.header.c_str(), header.value.c_str());
                //headers.insert(header);
                //
                if (remove) {
                    click_chatter("Removing");
                    CTXElement::removeBytes(packet, pos, end - current + 2);
                    current = (char*)packet->getPacketContent() + pos;
                } else {
                    current = end + 2;
                }
            } while(true);
            current += 2;


            if (!_set10 && fcb->CLRemoved) {
                //TODO : add chunked encoding
            }

            if(current != NULL)
            {
                int offset = (int)(current - (char*)packet->data());
                int headerSize =  offset;
                packet->setContentOffset(headerSize);
                fcb->headerFound = headerSize;
                if (unlikely(_verbose))
                    click_chatter("Header size %d. First two bytes : %x%x", headerSize, packet->getPacketContent()[0], packet->getPacketContent()[1]);
            }


        }

        // Add the size of the HTTP content to the counter
        uint16_t currentContent = packet->getPacketContentSize();
        fcb->contentSeen += currentContent;

        // Check if we have seen all the HTTP content
        if (_verbose)
            click_chatter("Seen %d (current %d), removed %d, length %d", fcb->contentSeen, currentContent, fcb->contentRemoved, fcb->contentLength);

        if(fcb->contentSeen >= fcb->contentLength) {
            if (_verbose)
                click_chatter("Last usefull packet !");
            setAnnotationLastUseful(packet, true);
        }
        return true;
    };
    EXECUTE_FOR_EACH_PACKET_UNTIL(fnt,flow);
    if (flow)
	    output(0).push_batch(flow);
}
/*
void HTTPIn::setHeader(WritablePacket*, const char* header, String value) {

}*/

bool HTTPIn::removeHeader(WritablePacket* packet, const StringRef& header)
{
    unsigned char* source = getPayload(packet);

    // Search the header name
    unsigned char* beginning = (unsigned char*)searchInContent((char*)source, header,
        getPayloadLength(packet));

    if(beginning == NULL)
        return false;

    uint32_t lengthLeft = getPayloadLength(packet) - (beginning - source);

    // Search the end of the header
    unsigned char* end = (unsigned char*)searchInContent((char*)beginning, "\r\n", lengthLeft);
    if(end == NULL)
        return false;

    // Compute the size of the header
    unsigned nbBytesToRemove = (end - beginning) + 2; //2 is strlen("\r\n");

    uint32_t position = beginning - source;

    // Remove data corresponding to the header
    CTXElement::removeBytes(packet, position, nbBytesToRemove);
    return true;
}

/*TODO : has header*/

bool HTTPIn::getHeaderContent(struct fcb_httpin *fcb, WritablePacket* packet, const StringRef &headerName,
     char* buffer, uint32_t bufferSize)
{
    unsigned char* source = getPayload(packet);
    // Search the header name
    unsigned char* beginning = (unsigned char*)searchInContent((char*)source, headerName,
        getPayloadLength(packet));

    if(beginning == NULL)
    {
        buffer[0] = '\0';
        return false;
    }

    // Skip the colon
    beginning += headerName.length() + 1;

    uint32_t lengthLeft = getPayloadLength(packet) - (beginning - source);

    // Search the end of the header
    unsigned char* end = (unsigned char*)searchInContent((char*)beginning, "\r\n", lengthLeft);
    if(end == NULL)
    {
        buffer[0] = '\0';
        return false;
    }

    // Skip spaces at the beginning of the string
    while(beginning < end && beginning[0] == ' ')
        beginning++;

    uint16_t contentSize = end - beginning;

    if(contentSize >= bufferSize)
    {
        contentSize = bufferSize - 1;
        click_chatter("Warning: buffer not big enough to contain the header %s", String(headerName.data()).c_str());
    }

    memcpy(buffer, beginning, contentSize);
    buffer[contentSize] = '\0';
    return true;
}

WritablePacket* HTTPIn::setHTTP10(struct fcb_httpin *fcb, WritablePacket *packet, int &endFirstLine)
{
    unsigned char* source = getPayload(packet);

    // Search the HTTP version
    unsigned char* beginning = (unsigned char*)searchInContent((char*)source, "HTTP/",
        endFirstLine);

    if(beginning == NULL)
        return packet;

    uint32_t lengthLeft = getPayloadLength(packet) - (beginning - source);

    unsigned char* endVersion = (unsigned char*)searchInContent((char*)beginning, " ", lengthLeft);
    if(endVersion == NULL || endVersion > source + endFirstLine)
        endVersion = source + endFirstLine;

    // Ensure that the line has the right length
    int offset = endVersion - beginning - 8; // 8 is the length of "HTTP/1.1"
    if(offset > 0)
        CTXElement::removeBytes(packet, beginning - source + 8, offset);
    else
        packet = CTXElement::insertBytes(packet , beginning - source + 5, -offset);

    endFirstLine -= offset;

    beginning[5] = '1';
    beginning[6] = '.';
    beginning[7] = '0';

    return packet;
}

/*void HTTPIn::setRequestParameters(struct fcb_httpin *fcb, WritablePacket *packet)
{

    unsigned char* source = getPayload(packet);
    // Search the end of the first line
    unsigned char* endFirstLine = (unsigned char*)searchInContent((char*)source, "\r\n",
        getPayloadLength(packet));

    if(endFirstLine == NULL)
        return;

    // Search the beginning of the URL (after the first space)
    unsigned char* urlStart = (unsigned char*)searchInContent((char*)source, " ",
        getPayloadLength(packet));

    if(urlStart == NULL || urlStart >= endFirstLine)
        return;

    // Get the HTTP method (before the URL)
    uint16_t methodLength = urlStart - source;
    if(methodLength >= 16)
        methodLength = 15;

    memcpy(fcb->method, source, methodLength);
    fcb->method[methodLength] = '\0';

    uint32_t lengthLeft = getPayloadLength(packet) - (urlStart - source);

    // Search the end of the URL (before the second space)
    unsigned char* urlEnd = (unsigned char*)searchInContent((char*)(urlStart + 1), " ", lengthLeft - 1);

    if(urlEnd == NULL || urlEnd >= endFirstLine)
        return;

    uint16_t urlLength = urlEnd - urlStart - 1;

    if(urlLength >= 2048)
        urlLength = 2047;

    memcpy(fcb->url, urlStart + 1, urlLength);
    fcb->url[urlLength] = '\0';

    fcb->isRequest = true;
}
*/

bool HTTPIn::isLastUsefulPacket(Packet *packet)
{
    return (getAnnotationLastUseful(packet) || CTXElement::isLastUsefulPacket(packet));
}

void HTTPIn::removeBytes(WritablePacket* packet, uint32_t position, uint32_t length)
{
    fcb_data()->contentRemoved += length;

    if (_fill == RESIZE_FILL) {
        memset((char*)packet->getPacketContent() + position, length, ' ');
        return;
    }

    // TODO chunked mode (or maybe catch that in the output)

    // Continue in the stack function if not fill
    CTXElement::removeBytes(packet, position, length);
}

WritablePacket*
HTTPIn::insertBytes(WritablePacket* packet, uint32_t position, uint32_t length)
{
    fcb_data()->contentRemoved -= length;

    // Continue in the stack function
    return CTXElement::insertBytes(packet, position, length);
}

CLICK_ENDDECLS

EXPORT_ELEMENT(HTTPIn)
ELEMENT_MT_SAFE(HTTPIn)
