#include <click/config.h>


#include "httpserver.hh"

#include <click/args.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <sys/select.h>

CLICK_DECLS

static MHD_Result ahc_policy(void *cls, const struct sockaddr *addr, socklen_t addrlen) {
    (void)cls;
    (void)addr;
    (void)addrlen;
    return MHD_YES;
}

HTTPServer::HTTPServer() : _port(80), _alias_map(), _verbose(false),  _daemon(0) {
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

    if (MHD_get_fdset(_daemon, &_read_fd_set, &_write_fd_set, &_except_fd_set, &max_fd) != MHD_YES) {
        click_chatter("Could not get fd set");
        return;
    }
    for (int i = 0; i <= max_fd; i++) {
        if (FD_ISSET(i, &_read_fd_set)) {
            add_select(i,SELECT_READ);
        }
        if (FD_ISSET(i, &_write_fd_set)) {
            add_select(i,SELECT_WRITE);
        }
    }
}

int HTTPServer::configure(Vector<String> &conf, ErrorHandler *errh) {
    Vector<String> alias_map;
    if (Args(conf, this, errh)
            .read_p("PORT", _port) //TODO AUTH
            .read_all("ALIAS_MAP", alias_map)
            .read("VERBOSE", _verbose)
            .complete() < 0)
        return -1;

    for (String a : alias_map) {
        int s = a.find_left(':');
        if (s == -1)
            return errh->error("Cannot find ':' in alias %s",a.c_str());
        String path = a.substring(0,s);
        if (path[0] == '/')
            path = path.substring(1);
        _alias_map.insert(path,String(a.substring(s+1)));
    }

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


MHD_Result HTTPServer::ahc_echo(
        void *cls,
        struct MHD_Connection *connection,
        const char *url,
        const char *method,
        const char *version,
        const char *upload_data,
        size_t *upload_data_size,
        void **con_cls) {
    HTTPServer *server = reinterpret_cast<HTTPServer *>(cls);

    int ret = MHD_YES;

    if (NULL == *con_cls)
        {*con_cls = new String("");

          return MHD_YES;
        }

    if (server->_verbose)
    click_chatter("[%s] %s",method,url);


    //Processing request
    String body;
    int status;
    struct MHD_Response *response;
    const Handler *h;

    String path = String(&url[1]);
    if (path[0] == '/')
        path = path.substring(1);

    auto it = server->_alias_map.findp(path);
    if (it != 0) {
        path = *it;
    }

    Element *e = 0;
    String ename;
    String fullpath = "";
    do {
        int pos = path.find_left('/');
        String en;
        if (pos == -1) {
            en = path;
        } else {
            en = path.substring(0,pos);
        }

        Element* et = server->router()->find(fullpath + en);
        if (!et) {
            break;
        }
        e = et;
        ename = en;
        fullpath = en + "/";
        if (pos == -1) {
            path = "";
            break;
        } else {
            path = path.substring(pos + 1);
        }
    } while (1);


    String hname;
    String param;
    int isNotPost = strcmp (method, "POST");
    int isNotPut = strcmp (method, "PUT");
    int pos = path.find_left('/');
    if (pos == -1) {
        hname = path;
        param = "";
    } else {
        hname = path.substring(0,pos);
        param = path.substring(pos + 1);
    }

    if (server->_verbose) {
        click_chatter("Element '%s', handler '%s', param '%s'",ename.c_str(), hname.c_str(),param.c_str());
    }

    if (!e) {
        if (hname == "" || ename == "") {
            e = server->router()->root_element();
        } else {
		goto e404;
        }
    }


    if ((hname == "") && strcmp("GET",method) == 0) {
        /*Json jelements = Json::make_array();
        for (int i = 0; i < server->router()->nelements(); i++) {
            String ename = server->router()->element(i)->name();
            Json je = Json::make_object();
            je.set("name",ename);
            jelements.push_back(je);
        }
        body = jelements.unparse();
        status = MHD_HTTP_OK;
        goto send;*/
        if (ename.length() > 0) {
            hname = "handlers";
        } else {
            hname = "list";
        }
    }

    // Then find handler.
    if (strcmp("GET",method) == 0) {
        h = Router::handler(e, hname);
        if (h && h->visible()) {
            if (h->readable()) {
                if (h->flags() & Handler::f_read_param) {
                    body = h->call_read(e, param, ErrorHandler::default_handler());
                } else {
                    body = h->call_read(e, ErrorHandler::default_handler());
                }
                status = MHD_HTTP_OK;
            } else {
                body = "This request is not readable";
                status = MHD_HTTP_BAD_REQUEST;
            }
            goto send;
        } else {
            goto bad_handler;
        }
    } else if (0 == isNotPost or 0 == isNotPut) {
        if (isNotPost) {
            hname = "put_" + hname;
        }
        h = Router::handler(e, hname);
        if (h && h->visible()) {
          if (*upload_data_size != 0) {
              static_cast<String*>(*con_cls)->append(String(upload_data, *upload_data_size));
              *upload_data_size = 0;

              return MHD_YES;
          } else {
              String data = *static_cast<String*>(*con_cls);
              if (server->_verbose)
                click_chatter("Last call to %s with data %s",path.c_str(), data.c_str());
              if (h->writable()) {
                  int ret;
                  if (isNotPost)
                      ret = h->call_write(param + "\n" + data, e, ErrorHandler::default_handler());
                  else
                      ret = h->call_write(data, e, ErrorHandler::default_handler());
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
              delete static_cast<String*>(*con_cls);
          }
          goto send;
        } else {
            goto bad_handler;
        }
    } else if (strcmp("DELETE",method) == 0) {
        hname = "delete_" + hname;
        h = Router::handler(e, hname);
        if (h && h->visible()) {
            int ret = h->call_write(param, e, ErrorHandler::default_handler());
            if (ret == 0) {
                body = "success";
            } else {
                body = "error";
            }
            status = MHD_HTTP_OK;
            goto send;
        } else {
            goto bad_handler;
        }
    } else {
        body = "Unsupported method";
        status = MHD_HTTP_METHOD_NOT_ALLOWED;
        goto send;
    }
    assert(false);

    bad_handler:
	body = "Invalid path '" + String(url);
	if (ename)
		body += "' or no " + hname + " in " + ename;
	else
		body += "' or no " + hname + " in root";
	status = 404;
    if (server->_verbose)
	click_chatter(("404 : " + body).c_str());
	goto send;

	e404:
    body =  "No element named '" + ename + "'";
    status = 404;
    if (server->_verbose)
	click_chatter(("404 : " + body).c_str());
    goto send;

    send:
    response = MHD_create_response_from_buffer (body.length(),
            (void*)body.c_str(),
            MHD_RESPMEM_MUST_COPY);
    if (NULL == response) {
        click_chatter("Could not create response");
        return MHD_NO;
    }
    ret = MHD_queue_response(connection,
            status,
            response);
    MHD_destroy_response(response);

    return ret == 0? MHD_NO : MHD_YES;
}

void HTTPServer::cleanup(CleanupStage) {
    if (_daemon != NULL)
        MHD_stop_daemon(_daemon);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel microhttpd)
EXPORT_ELEMENT(HTTPServer)
ELEMENT_MT_SAFE(HTTPServer)
