#include <click/config.h>


#include <microhttpd.h>
#include "httpserver.hh"

#include <click/args.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <sys/select.h>

CLICK_DECLS


static int ahc_policy(void* cls, const struct sockaddr * addr, socklen_t addrlen) {
    (void)cls;
    (void)addr;
    (void)addrlen;
	return MHD_YES;
}


static String
canonical_handler_name(const String &n)
{
	const char *dot = find(n, '/');
	if (dot == n.begin() || (dot == n.begin() + 1 && n.front() == '0'))
		return n.substring(dot + 1, n.end());
	else
		return n;
}

HTTPServer::HTTPServer() : _port(80), _daemon(0) {
}

HTTPServer::~HTTPServer() {

}


void HTTPServer::selected(int fd, int mask) {
	remove_select(fd, mask);
	MHD_run(_daemon);
	update_fd_set();
}

void HTTPServer::update_fd_set() {
	int max_fd = 0;
    fd_set _read_fd_set,_write_fd_set,_except_fd_set;

	FD_ZERO(&_read_fd_set);
	FD_ZERO(&_write_fd_set);
	FD_ZERO(&_except_fd_set);

	if (MHD_get_fdset(_daemon,&_read_fd_set,&_write_fd_set,&_except_fd_set,&max_fd) != MHD_YES) {
		click_chatter("Could not get fd set");
		return;
	}
	for (int i = 0; i <= max_fd; i++) {
		if (FD_ISSET(i,&_read_fd_set)) {
			add_select(i,SELECT_READ);
		}
		if (FD_ISSET(i,&_write_fd_set)) {
			add_select(i,SELECT_WRITE);
		}
	}
}

int HTTPServer::configure(Vector<String> &conf, ErrorHandler *errh) {
	if (Args(conf, this, errh)
			.read_p("PORT", _port) //TODO AUTH
			.complete() < 0)
		return -1;

	return 0;
}


int HTTPServer::initialize(ErrorHandler *errh) {
	_daemon = MHD_start_daemon(MHD_USE_DEBUG,
			_port,
			&ahc_policy,
			NULL,
			&ahc_echo,
			(void*)this,
			MHD_OPTION_END);
	if (_daemon == NULL)
		return 1;

	update_fd_set();

	return 0;
}


int HTTPServer::ahc_echo(void * cls,
		struct MHD_Connection * connection,
		const char * url,
		const char * method,
		const char * version,
		const char * upload_data,
		size_t * upload_data_size,
		void ** ptr) {
	static int dummy;
	HTTPServer* server = reinterpret_cast<HTTPServer*>(cls);
click_chatter("echo");

	int ret = MHD_YES;

	/* if (0 != strcmp(method, "GET"))
    return MHD_NO; */

	if (&dummy != *ptr)
	{	
		*ptr = &dummy;
		return MHD_YES;
	}
	click_chatter("[%s] %s",method,url);

	*ptr = NULL; /* clear context pointer */

	//Processing request
	Request* request = new Request();
	request->connection = connection;
	String body;
	int status;
	struct MHD_Response * response;
	const Handler* h;

	//Following is taken from ControlSocket mostly
	String full_name = String(&url[1]);
	String canonical_name = canonical_handler_name(full_name);
	Element *e;
	const char *dot = find(canonical_name, '/');
	String hname;

	if (dot != canonical_name.end()) {
		String ename = canonical_name.substring(canonical_name.begin(), dot);
		e = server->router()->find(ename);
		if (!e) {
			int num;
			if (IntArg().parse(ename, num) && num > 0 && num <= server->router()->nelements())
				e = server->router()->element(num - 1);
		}
		if (!e) {
			body =  "No element named '" + ename + "'";
			status = 404;
			goto send;
		}
		hname = canonical_name.substring(dot + 1, canonical_name.end());
	} else {
		e = server->router()->root_element();
		hname = canonical_name;
	}

	// Then find handler.
	h = Router::handler(e, hname);
	if (h && h->visible()) {
	    if (strcmp("GET",method) == 0) {
	        if (h->readable()) {
	            body = h->call_read(e, ErrorHandler::default_handler());
	            status = MHD_HTTP_OK;
	        } else {
	            body = "This request is not readable";
	            status = MHD_HTTP_BAD_REQUEST;
	        }
	    } else {
	        String data = String(upload_data);
	        if (h->writable()) {
	            int ret = h->call_write(data, e, ErrorHandler::default_handler());
	            if (ret == 0) {
	                body = "success";
	            } else {
	                body = "error";
	            }
                status = MHD_HTTP_OK;
	        } else {
	            body = "This request is not writable";
	            status = MHD_HTTP_BAD_REQUEST;
	        }
	    }
		goto send;
	} else {
		body = "No handler named '" + full_name + "'";
		status = 404;
		goto send;
	}

	send:
	response = MHD_create_response_from_buffer (body.length(),
			(void*)body.c_str(),
			MHD_RESPMEM_MUST_COPY);
	if (NULL == response)
	    return MHD_NO;
	ret = MHD_queue_response(request->connection,
			status,
			response);
	MHD_destroy_response(response);

	return ret;
}

void HTTPServer::cleanup(CleanupStage) {
	if (_daemon != NULL)
		MHD_stop_daemon(_daemon);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(HTTPServer)
ELEMENT_MT_SAFE(HTTPServer)
ELEMENT_LIBS((-lmicrohttpd))
