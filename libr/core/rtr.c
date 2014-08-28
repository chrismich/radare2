/* radare - Copyright 2009-2014 - pancake, nibble */

#include "r_core.h"
#include "r_socket.h"

#define endian core->assembler->big_endian
#define rtr_n core->rtr_n
#define rtr_host core->rtr_host

static RSocket *s = NULL;
static const char *listenport = NULL;

typedef struct {
	const char *host;
	const char *port;
	const char *file;
} TextLog;

static char *rtrcmd (TextLog T, const char *str) {
	int len;
	char uri[1024];
	char *res, *ptr2, *ptr = r_str_uri_encode (str);
	if (ptr) str = ptr;
	snprintf (uri, sizeof (uri), "http://%s:%s/%s%s",
		T.host, T.port, T.file, str);
	if (ptr == str) free (ptr);
	ptr2 = r_socket_http_get (uri, NULL, &len);
	if (ptr2) {
		ptr2[len] = 0;
		res = strstr (ptr2, "\n\n");
		if (res) res = strstr (res+1, "\n\n");
		if (res) res += 2; else res = ptr2;
		return ptr2;
	}
	return NULL;
}

static int rtr_visual (RCore *core, TextLog T, const char *cmd) {
	if (cmd) {
		r_cons_break (NULL, NULL);
		for (;;) {
			r_cons_clear00 ();
			r_cons_printf ("%s\n", rtrcmd (T, cmd));
			r_cons_flush ();
			if (r_cons_singleton ()->breaked)
				break;
			r_sys_sleep (1);
		}
		r_cons_break_end ();
	} else {
		const char *cmds[] = { "px", "pd", "pxa", NULL };
		int cmdidx = 0;
		char *ret, ch;
		for (;;) {
			r_cons_clear00 ();
			ret = rtrcmd (T, cmds[cmdidx]);
			if (ret) {
				r_cons_printf ("%s\n", ret);
				free (ret);
			}
			r_cons_flush ();
			ch = r_cons_readchar ();
#if 0
TODO: 
 :   prompt
 i   insert hex/string/asm
 0-9 follow jumps
#endif
			switch (ch) {
			case '?': 
				r_cons_clear00();
				r_cons_printf ("Remote Visual keys:\n");
				r_cons_printf (" hjkl : move\n");
				r_cons_printf (" pP   : rotate print modes\n");
				r_cons_printf (" q    : quit this mode and go back to the shell\n");
				r_cons_flush ();
				r_cons_any_key ();
				break;
			case 'j': free (rtrcmd (T, "s+16")); break;
			case 'k': free (rtrcmd (T, "s-16")); break;
			case 'h': free (rtrcmd (T, "s-1")); break;
			case 'l': free (rtrcmd (T, "s+1")); break;
			case 'p': cmdidx++; if (!cmds[cmdidx]) cmdidx = 0; break;
			case 'P': cmdidx--; if (cmdidx<0) cmdidx = 2; break;
			case 'q':
				return 0;
			}
		}
	}
	return 1;
}

// TODO: rename /name to /nick or /so?
// clone of textlog_chat () using rtrcmd()
static int rtr_textlog_chat (RCore *core, TextLog T) {
	char prompt[64];
	char buf[1024];
	int lastmsg = 0;
	const char *me = r_config_get (core->config, "cfg.user");
	char *ret, msg[1024];

	eprintf ("Type '/help' for commands:\n");
	char *oldprompt = strdup (r_line_singleton ()->prompt);
	snprintf (prompt, sizeof (prompt)-1, "[%s]> ", me);
	r_line_set_prompt (prompt);
	ret = rtrcmd (T, msg);
	for (;;) {
		if (lastmsg) {
			snprintf (msg, sizeof (msg)-1, "T %d", lastmsg);
		} else {
			strcpy (msg, "T");
}
		ret = rtrcmd (T, msg);
		r_cons_printf ("%s\n", ret);
		free (ret);
		ret = rtrcmd (T, "Tl");
		lastmsg = atoi (ret)-1;
		free (ret);
		if (r_cons_fgets (buf, sizeof (buf)-1, 0, NULL)<0)
			goto beach;
		if (!*buf) continue;
		if (!strcmp (buf, "/help")) {
			eprintf ("/quit           quit the chat (same as ^D)\n");
			eprintf ("/name <nick>    set cfg.user name\n");
			eprintf ("/log            show full log\n");
			eprintf ("/clear          clear text log messages\n");
		} else if (!strncmp (buf, "/name ", 6)) {
			snprintf (msg, sizeof (msg)-1, "* '%s' is now known as '%s'", me, buf+6);
			r_core_log_add (core, msg);
			r_config_set (core->config, "cfg.user", buf+6);
			me = r_config_get (core->config, "cfg.user");
			snprintf (prompt, sizeof (prompt)-1, "[%s]> ", me);
			r_line_set_prompt (prompt);
		} else if (!strcmp (buf, "/log")) {
			char *ret = rtrcmd (T, "T");
			if (ret) {
				r_cons_printf ("%s\n", ret);
				free (ret);
			}
		} else if (!strcmp (buf, "/clear")) {
			//r_core_log_del (core, 0);
			free (rtrcmd (T, "T-"));
		} else if (!strcmp (buf, "/quit")) {
			goto beach;
		} else if (*buf=='/') {
			eprintf ("Unknown command: %s\n", buf);
		} else {
			snprintf (msg, sizeof (msg)-1, "T [%s] %s", me, buf);
			free (rtrcmd (T, msg));
		}
	}
beach:
	r_line_set_prompt (oldprompt);
	free (oldprompt);
	return 1;
}

#define http_break r_core_rtr_http_stop
R_API int r_core_rtr_http_stop(RCore *u) {
	RSocket* sock;
	const char *port;
	const int timeout = 1; // 1 second
	RCore *core = (RCore*)u;
	if (((size_t)u)>0xff) {
		port = listenport? listenport: r_config_get (
			core->config, "http.port");
		sock = r_socket_new (0);
		(void)r_socket_connect (sock, "localhost",
			port, R_SOCKET_PROTO_TCP, timeout);
		r_socket_free (sock);
	}
	r_socket_free (s);
	s = NULL;
	return 0;
}

#if 0
SECURITY IMPLICATIONS
- no ssl
- no auth
- commands can be executed by anyone
- default is to listen on localhost
- can access full filesystem
- follow symlinks
#endif

static char *rtr_dir_files (const char *path) {
	char *ptr = strdup ("<html><body>\n");
	const char *file;
	RListIter *iter;
	// list files
	RList *files = r_sys_dir (path);
	eprintf ("Listing directory %s\n", path);
	r_list_foreach (files, iter, file) {
		if (file[0] == '.') continue;
		ptr = r_str_concatf (ptr, "<a href=\"%s%s\">%s</a><br />\n",
			path, file, file);
	}
	r_list_free (files);
	return r_str_concat (ptr, "</body></html>\n");
}


typedef struct {
	RCore *core;
	int launch;
	const char *path;
} HttpThread;

static int r_core_rtr_http_run (RCore *core, int launch, const char *path) {
	char buf[32];
	RSocketHTTPRequest *rs;
	int iport, oldsandbox = -1;
	int timeout = r_config_get_i (core->config, "http.timeout");
	int x = r_config_get_i (core->config, "scr.html");
	int y = r_config_get_i (core->config, "scr.color");
	int z = r_config_get_i (core->config, "asm.bytes");
	int u = r_config_get_i (core->config, "scr.interactive");
	int v = r_config_get_i (core->config, "asm.cmtright");
	const char *port = r_config_get (core->config, "http.port");
	char *allow = (char *)r_config_get (core->config, "http.allow");
	if (!strcmp (port, "0")) {
		r_num_irand ();
		iport = 1024+r_num_rand (45256);
		snprintf (buf, sizeof (buf), "%d", iport);
		port = buf;
	}
	RSocket *s;
	s = r_socket_new (R_FALSE);
	s->local = !r_config_get_i (core->config, "http.public");
	if (!r_socket_listen (s, port, NULL)) {
		eprintf ("Cannot listen on http.port\n");
		return 1;
	}
	if (launch) {
		char cmd[128];
		const char *browser = r_config_get (core->config, "http.browser");
		snprintf (cmd, sizeof (cmd)-1, "%s http://localhost:%d/%s &",
			browser, atoi (port), path?path:"");
		r_sys_cmd (cmd);
	}
	r_config_set (core->config, "asm.cmtright", "false");
	r_config_set (core->config, "scr.html", "true");
	r_config_set (core->config, "scr.color", "false");
	r_config_set (core->config, "asm.bytes", "false");
	r_config_set (core->config, "scr.interactive", "false");
	if (r_config_get_i (core->config, "http.sandbox")) {
		oldsandbox = r_config_get_i (core->config, "cfg.sandbox");
		r_config_set (core->config, "cfg.sandbox", "true");
	}
	eprintf ("Starting http server...\n");
	eprintf ("http://localhost:%d/\n", atoi (port));
	core->http_up = R_TRUE;
	while (!r_cons_singleton ()->breaked) {
		r_cons_break ((RConsBreak)http_break, core);
		rs = r_socket_http_accept (s, timeout);
		if (!rs) {
			if (!s) break;
			r_sys_usleep (100);
			continue;
		}
		if (allow && *allow) {
			int accepted = R_FALSE;
			const char *host;
			char *p, *peer = r_socket_to_string (rs->s);
			char *allows = strdup (allow);
			//eprintf ("Firewall (%s)\n", allows);
			int i, count = r_str_split (allows, ',');
			p = strchr (peer, ':');
			if (p) *p = 0;
			for (i=0; i<count; i++) {
				host = r_str_word_get0 (allows, i);
				//eprintf ("--- (%s) (%s)\n", host, peer);
				if (!strcmp (host, peer)) {
					accepted = R_TRUE;
					break;
				}
			}
			free (peer);
			free (allows);
			if (!accepted) {
				r_socket_http_close (rs);
				continue;
			}
		}
		if (!rs->method || !rs->path) {
			eprintf ("Invalid http headers received from client\n");
			r_socket_http_close (rs);
			continue;
		}
		char *dir = NULL;

		if (r_config_get_i (core->config, "http.dirlist"))
			if (r_file_is_directory (rs->path))
				dir = strdup (rs->path);
		if (!strcmp (rs->method, "GET")) {
			if (!memcmp (rs->path, "/up", 3)) {
				if (r_config_get_i (core->config, "http.upget")) {
					const char *uproot = r_config_get (core->config, "http.uproot");
					if (!rs->path[3] || (rs->path[3]=='/'&&!rs->path[4])) {
						char *ptr = rtr_dir_files (uproot);
						r_socket_http_response (rs, 200, ptr, 0, NULL);
						free (ptr);
					} else {
						char *path = r_file_root (uproot, rs->path + 4);
						if (r_file_exists (path)) {
							int sz = 0;
							char *f = r_file_slurp (path, &sz);
							if (f) {
								r_socket_http_response (rs, 200, f, sz, NULL);
								free (f);
							} else {
								r_socket_http_response (rs, 403, "Permission denied", 0, NULL);
								eprintf ("http: Cannot open '%s'\n", path);
							}
						} else {
							if (dir) {
								char *resp = rtr_dir_files (dir);
								r_socket_http_response (rs, 404, resp, 0, NULL);
								free (resp);
							} else {
								eprintf ("File '%s' not found\n", path);
								r_socket_http_response (rs, 404, "File not found\n", 0, NULL);
							}
						}
						free (path);
					}
				} else {
					r_socket_http_response (rs, 403,
							"Permission denied\n", 0, NULL);
				}
			} else if (!memcmp (rs->path, "/cmd/", 5)) {
				char *cmd = rs->path +5;
				char foo[32];
				const char *httpcmd = r_config_get (core->config, "http.uri");
				while (*cmd=='/') cmd++;
				if (httpcmd && *httpcmd) {
					int len;
					char *res;
					// do remote http query and proxy response
					snprintf (foo, sizeof (foo), "%s/%s", httpcmd, cmd);
					res = r_socket_http_get (foo, NULL, &len);
					if (res) {
						res[len]=0;
						r_cons_printf ("%s\n", res);
					}
				} else {
					char *out, *cmd = rs->path+5;
					r_str_uri_decode (cmd);
					// eprintf ("CMD (%s)\n", cmd);
					out = r_core_cmd_str_pipe (core, cmd);
					// eprintf ("\nOUT LEN = %d\n", strlen (out));
					if (out) {
						char *res = r_str_uri_encode (out);
						r_socket_http_response (rs, 200, out, 0,
							"Content-Type: text/plain\n");
						free (out);
						free (res);
					} else r_socket_http_response (rs, 200, "", 0, NULL);
				}
			} else {
				const char *root = r_config_get (core->config, "http.root");
				char *path = r_file_root (root, rs->path);
				// FD IS OK HERE
				if (rs->path [strlen (rs->path)-1] == '/') {
					path = r_str_concat (path, "index.html");
					//rs->path = r_str_concat (rs->path, "index.html");
				} else {
					//snprintf (path, sizeof (path), "%s/%s", root, rs->path);
					if (r_file_is_directory (path)) {
						char res[128];
						snprintf (res, sizeof (res),
							"Location: %s/\n", rs->path);
						r_socket_http_response (rs, 302,
							NULL, 0, res);
						r_socket_http_close (rs);
						free (path);
						free (dir);
						dir = NULL;
						continue;
					}
				}
				if (r_file_exists (path)) {
					int sz = 0;
					char *f = r_file_slurp (path, &sz);
					if (f) {
						const char *contenttype = NULL;
						if (strstr (path, ".js")) contenttype = "Content-Type: application/javascript\n";
						if (strstr (path, ".css")) contenttype = "Content-Type: text/css\n";
						if (strstr (path, ".html")) contenttype = "Content-Type: text/html\n";
						r_socket_http_response (rs, 200, f, sz, contenttype);
						free (f);
					} else {
						r_socket_http_response (rs, 403, "Permission denied", 0, NULL);
						eprintf ("http: Cannot open '%s'\n", path);
					}
				} else {
					if (dir) {
						char *resp = rtr_dir_files (dir);
						eprintf ("Dirlisting %s\n", dir);
						r_socket_http_response (rs, 404, resp, 0, NULL);
						free (resp);
					} else {
						eprintf ("File '%s' not found\n", path);
						r_socket_http_response (rs, 404, "File not found\n", 0, NULL);
					}
				}
				free (path);
			}
		} else 
		if (!strcmp (rs->method, "POST")) {
			ut8 *ret;
			int retlen;
			char buf[128];
			if (r_config_get_i (core->config, "http.upload")) {
				ret = r_socket_http_handle_upload (
					rs->data, rs->data_length, &retlen);
				if (ret) {
					ut64 size = r_config_get_i (core->config, "http.maxsize");
					if (size && retlen > size) {
						r_socket_http_response (rs, 403, "403 File too big\n", 0, NULL);
					} else {
						char *filename = r_file_root (
							r_config_get (core->config, "http.uproot"),
							rs->path + 4);
						eprintf ("UPLOADED '%s'\n", filename);
						r_file_dump (filename, ret, retlen);
						free (filename);
						snprintf (buf, sizeof (buf),
							"<html><body><h2>uploaded %d bytes. Thanks</h2>\n", retlen);
							r_socket_http_response (rs, 200, buf, 0, NULL);
					}
					free (ret);
				}
			} else {
				r_socket_http_response (rs, 403, "403 Forbidden\n", 0, NULL);
			}
		} else {
			r_socket_http_response (rs, 404, "Invalid protocol", 0, NULL);
		}
		r_socket_http_close (rs);
		free (dir);
	}
	core->http_up = R_FALSE;
	r_socket_free (s);
	r_cons_break_end ();
	r_config_set_i (core->config, "scr.html", x);
	r_config_set_i (core->config, "scr.color", y);
	r_config_set_i (core->config, "asm.bytes", z);
	r_config_set_i (core->config, "scr.interactive", u);
	r_config_set_i (core->config, "asm.cmtright", v);
	if (oldsandbox != -1)
		r_config_set_i (core->config, "cfg.sandbox", oldsandbox);
	return 0;
}

static int r_core_rtr_http_thread (RThread *th) {
	HttpThread *ht = th->user;
	return r_core_rtr_http_run (ht->core, ht->launch, ht->path);
}

static RThread *httpthread = NULL;

#define USE_THREADS 1

R_API int r_core_rtr_http(RCore *core, int launch, const char *path) {
	const char *port;

	if (r_sandbox_enable (0)) {
		eprintf ("sandbox: connect disabled\n");
		return 1;
	}
	if (core->http_up) {
		eprintf ("http server is already running\n");
		return 1;
	}
	if (path && atoi (path)) {
		port = path;
		path = NULL;
	}

	if (launch==2) {
	if (httpthread) {
		eprintf ("HTTP Thread is already running\n");
		eprintf ("This is experimental and probably buggy. Use at your own risk\n");
		eprintf ("TODO: Add a command to kill that thread\n");
		eprintf ("TODO: Use different eval environ for scr. for the web\n");
		eprintf ("TODO: Visual mode should be enabled on local\n");
	} else {
		HttpThread ht = { core, launch, path };
		httpthread = r_th_new (r_core_rtr_http_thread, &ht, 0);
		r_th_start (httpthread, 1);
		eprintf ("Background http server started.\n");
	}
	return 0;
	}
	return r_core_rtr_http_run (core, launch, path);
}

R_API void r_core_rtr_help(RCore *core) {
	const char* help_msg[] = {
	"Usage:", " =[:!+-=hH] [...]", " # radare remote command execution protocol",
	"\nrap commands:", "", "",
	"=", "", "list all open connections",
	"=<", "[fd] cmd", "send output of local command to remote fd",
	"=", "[fd] cmd", "exec cmd at remote 'fd' (last open is default one)",
	"=!", " cmd", "run command via r_io_system",
	"=+", " [proto://]host", "add host (default=rap://, tcp://, udp://)",
	"=-", "[fd]", "remove all hosts or host 'fd'",
	"==", "[fd]", "open remote session with host 'fd', 'q' to quit",
	"\nrap server:","","",
	"=", ":port", "listen on given port using rap protocol (o rap://9999)",
	"=", ":host:port cmd", "run 'cmd' command on remote server",
	"\nhttp server:", "", "",
	"=h", "", "listen for http connections (r2 -qc=H /bin/ls)",
	"=H", "", "launch browser and listen for http",
	NULL};
	r_core_cmd_help (core, help_msg);
}

R_API void r_core_rtr_pushout(RCore *core, const char *input) {
	int fd = atoi (input);
	const char *cmd = NULL;
	char *str = NULL;
	if (fd) {
		for (rtr_n = 0; rtr_host[rtr_n].fd->fd != fd \
			&& rtr_n < RTR_MAX_HOSTS - 1; rtr_n++);
		if (!(cmd = strchr (input, ' '))) {
			eprintf ("Error\n");
			return;
		}
	} else cmd = input;

	if (!rtr_host[rtr_n].fd->fd) {
		eprintf("Error: Unknown host\n");
		return;
	}

	if (!(str = r_core_cmd_str (core, cmd))) {
		eprintf ("Error: radare_cmd_str returned NULL\n");
		return;
	}

	switch (rtr_host[rtr_n].proto) {
	case RTR_PROT_RAP:
		eprintf ("Error: Cannot use '=<' to a rap connection.\n");
		break;
	case RTR_PROT_TCP:
	case RTR_PROT_UDP:
	default:
		r_socket_write (rtr_host[rtr_n].fd, str, strlen (str));
		break;
	}
	free (str);
}

R_API void r_core_rtr_list(RCore *core) {
	int i;
	for (i = 0; i < RTR_MAX_HOSTS; i++)
		if (rtr_host[i].fd) {
			r_cons_printf("%i - ", rtr_host[i].fd->fd);
			if (rtr_host[i].proto == RTR_PROT_HTTP)
				r_cons_printf( "http://");
			if (rtr_host[i].proto == RTR_PROT_TCP)
				r_cons_printf ("tcp://");
			else if (rtr_host[i].proto == RTR_PROT_UDP)
				r_cons_printf("udp://");
			else r_cons_printf("rap://");
			r_cons_printf ("%s:%i/%s\n", rtr_host[i].host,
				rtr_host[i].port, rtr_host[i].file);
	}
}

R_API void r_core_rtr_add(RCore *core, const char *_input) {
	char *port, input[1024], *host = NULL, *file = NULL, *ptr = NULL, buf[1024];
	int proto, i, timeout, ret;
	RSocket *fd;

	timeout = r_config_get_i (core->config, "http.timeout");
	strncpy (input, _input, sizeof (input)-4);
	/* Parse uri */
	if ((ptr = strstr (input, "tcp://"))) {
		proto = RTR_PROT_TCP;
		host = ptr+6;
	} else if ((ptr = strstr(input, "http://"))) {
		proto = RTR_PROT_HTTP;
		host = ptr+7;
	} else if ((ptr = strstr(input, "udp://"))) {
		proto = RTR_PROT_UDP;
		host = ptr+6;
	} else if ((ptr = strstr(input, "rap://"))) {
		proto = RTR_PROT_RAP;
		host = ptr+6;
	} else {
		proto = RTR_PROT_RAP;
		host = input;
	}
	while (*host && iswhitechar (*host))
		host++;

	if (!(ptr = strchr (host, ':'))) {
		ptr = host;
		port = "80";
	} else {
		*ptr++ = '\0';
		port = ptr;
	}

	if (!(file = strchr (ptr, '/'))) {
		eprintf("Error: Missing '/'\n");
		return;
	}
	*file++ = 0;
	port = r_str_chop (port);
	while (*file==' ') file++;
	if (r_sandbox_enable (0)) {
		eprintf ("sandbox: connect disabled\n");
		return;
	}

	fd = r_socket_new (R_FALSE);
	if (!fd) {
		eprintf ("Error: Cannot create new socket\n");
		return;
	}
	switch (proto) {
	case RTR_PROT_HTTP:
		{
			char uri[1024], prompt[64];
			int len;
			char *str, *res;
			if (file[strlen (file)-1]=='/') {
				TextLog T = { host, port, file };
				snprintf (prompt, sizeof (prompt), "[http://%s:%s/%s]> ",
					host, port, file);
				r_line_set_prompt (prompt);
				for (;;) {
					char *ptr, *str = r_line_readline ();
					if (!str || !*str) break;
					if (*str == 'q') break;
					if (str[0]=='V') {
						if (str[1]==' ') {
							rtr_visual (core, T, str+1);
						} else {
							rtr_visual (core, T, NULL);
						}
					} else
					if (!strcmp (str, "TT")) {
						rtr_textlog_chat (core, T);

					} else {
					ptr = r_str_uri_encode (str);
					if (ptr) str = ptr;
					snprintf (uri, sizeof (uri), "http://%s:%s/%s%s",
						host, port, file, str);
					if (ptr == str) free (ptr);
					str = r_socket_http_get (uri, NULL, &len);
					if (str) {
						str[len] = 0;
						res = strstr (str, "\n\n");
						if (res) res = strstr (res+1, "\n\n");
						if (res) res += 2; else res = str;
						printf ("%s%s", res, (res[strlen (res)-1]=='\n')?"":"\n");
						r_line_hist_add (str);
						free (str);
					}
}
				}
				r_socket_free (fd);
				return;
			}
			snprintf (uri, sizeof (uri), "http://%s:%s/%s",
				host, port, file);
			str = r_socket_http_get (uri, NULL, &len);
			if (str) {
				str[len] = 0;
				res = strstr (str, "\n\n");
				if (res) res = strstr (res+1, "\n\n");
				if (res) res += 2; else res = str;
				printf ("%s", res);
				free (str);
			} else eprintf ("HTTP connection has failed\n");
			// do not add connection. wtf
			return;
		}
		break;
	case RTR_PROT_RAP:
		if (!r_socket_connect_tcp (fd, host, port, timeout)) { //TODO: Use rap.ssl
			eprintf ("Error: Cannot connect to '%s' (%s)\n", host, port);
			return;
		}
		eprintf ("Connected to %s at port %s\n", host, port);
		/* send */
		buf[0] = RTR_RAP_OPEN;
		buf[1] = 0;
		buf[2] = (ut8)(strlen (file)+1);
		memcpy (buf+3, file, buf[2]);
		r_socket_write(fd, buf, 3+buf[2]);
		/* read */
		eprintf ("waiting... "); fflush(stdout);
		r_socket_read (fd, (ut8*)buf, 5);
		r_mem_copyendian ((ut8 *)&i, (ut8*)buf+1, 4, core->assembler->big_endian);
		if (buf[0] != (char)(RTR_RAP_OPEN|RTR_RAP_REPLY) || i<= 0) {
			eprintf ("Error: Wrong reply\n");
			return;
		}
		eprintf ("ok\n");
		break;
	case RTR_PROT_TCP:
		if (!r_socket_connect_tcp (fd, host, port, timeout)) { //TODO: Use rap.ssl
			core->num->value = 1;
			eprintf("Error: Cannot connect to '%s' (%s)\n", host, port);
			return;
		}
		core->num->value = 0;
		eprintf ("Connected to: %s at port %s\n", host, port);
		break;
	case RTR_PROT_UDP:
		if (!r_socket_connect_udp (fd, host, port, timeout)) { //TODO: Use rap.ssl
			core->num->value = 1;
			eprintf ("Error: Cannot connect to '%s' (%s)\n", host, port);
			return;
		}
		core->num->value = 0;
		eprintf("Connected to: %s at port %s\n", host, port);
		break;
	}

	ret = core->num->value;
	for (i = 0; i < RTR_MAX_HOSTS; i++)
		if (!rtr_host[i].fd) {
			rtr_host[i].proto = proto;
			memcpy (rtr_host[i].host, host, 512);
			rtr_host[i].port = r_num_get (core->num, port);
			memcpy (rtr_host[i].file, file, 1024);
			rtr_host[i].fd = fd;
			rtr_n = i;
			break;
		}
	core->num->value = ret;
	r_socket_free(fd);
	//r_core_rtr_list (core);
}

R_API void r_core_rtr_remove(RCore *core, const char *input) {
	int fd, i;

	if (input[0] >= '0' && input[0] <= '9') {
		fd = r_num_math (core->num, input);
		for (i = 0; i < RTR_MAX_HOSTS; i++)
			if (rtr_host[i].fd->fd == fd) {
				r_socket_free (rtr_host[i].fd);
				rtr_host[i].fd = NULL;
				if (rtr_n == i)
					for (rtr_n = 0; !rtr_host[rtr_n].fd \
						&& rtr_n < RTR_MAX_HOSTS - 1; rtr_n++);
				break;
		}
	} else {
		for (i = 0; i < RTR_MAX_HOSTS; i++)
			if (rtr_host[i].fd)
				r_socket_free (rtr_host[i].fd);
		memset (rtr_host, '\0', RTR_MAX_HOSTS * sizeof(RCoreRtrHost));
		rtr_n = 0;
	}
}

R_API void r_core_rtr_session(RCore *core, const char *input) {
	char prompt[64], buf[4096];
	int fd;

	prompt[0] = 0;
	if (input[0] >= '0' && input[0] <= '9') {
		fd = r_num_math (core->num, input);
		for (rtr_n = 0; rtr_host[rtr_n].fd->fd != fd \
			&& rtr_n < RTR_MAX_HOSTS - 1; rtr_n++);
	}

	for (;;) {
		if (rtr_host[rtr_n].fd)
			snprintf (prompt, sizeof (prompt),
				"fd:%d> ", rtr_host[rtr_n].fd->fd);
		free (r_line_singleton ()->prompt);
		r_line_singleton ()->prompt = strdup (prompt);
		if ((r_cons_fgets (buf, sizeof (buf), 0, NULL))) {
			if (!*buf || *buf == 'q')
				break;
			if (*buf == 'V') {
				eprintf ("Visual mode not supported\n");
				continue;
			}
			r_core_rtr_cmd (core, buf);
			r_cons_flush ();
		} else break;
	}
}

R_API void r_core_rtr_cmd(RCore *core, const char *input) {
	char bufw[1024], bufr[8];
	const char *cmd = NULL, *cmd_output = NULL;
	int i, cmd_len, fd = atoi (input);

	if (*input==':' && !strchr (input+1, ':')) {
		r_core_cmdf (core, "o rap://%s", input);
		return;
	}
	if (fd != 0) {
		if (rtr_host[rtr_n].fd)
			for (rtr_n = 0; rtr_host[rtr_n].fd->fd != fd
				&& rtr_n < RTR_MAX_HOSTS - 1; rtr_n++);
		if (!(cmd = strchr (input, ' '))) {
			eprintf ("Error\n");
			return;
		}
	} else cmd = input;

	if (!rtr_host[rtr_n].fd){
		eprintf ("Error: Unknown host\n");
		core->num->value = 1; // fail
		return;
	}

	if (!rtr_host[rtr_n].proto == RTR_PROT_RAP){
		eprintf ("Error: Not a rap:// host\n");
		return;
	}

	core->num->value = 0; // that's fine
	if (!strlen (cmd)) {
		// just check if we can connect
		r_socket_close (rtr_host[rtr_n].fd);
		return;
	}
	/* send */
	bufw[0] = RTR_RAP_CMD;
	i = strlen (cmd) + 1;
	r_mem_copyendian ((ut8*)bufw+1, (ut8*)&i, 4, endian);
	memcpy (bufw+5, cmd, i);
	r_socket_write (rtr_host[rtr_n].fd, bufw, 5+i);
	/* read */
	r_socket_read (rtr_host[rtr_n].fd, (ut8*)bufr, 5);
	if (bufr[0] != (char)(RTR_RAP_CMD|RTR_RAP_REPLY)) {
		eprintf ("Error: Wrong reply\n");
		return;
	}
	r_mem_copyendian ((ut8*)&cmd_len, (ut8*)bufr+1, 4, endian);
	cmd_output = malloc (cmd_len);
	if (!cmd_output) {
		eprintf ("Error: Allocating cmd output\n");
		return;
	}
	r_socket_read (rtr_host[rtr_n].fd, (ut8*)cmd_output, cmd_len);
	r_cons_printf ("%s\n", cmd_output);
	free ((void *)cmd_output);
}

// TODO: support len for binary data?
R_API char *r_core_rtr_cmds_query (RCore *core, const char *host, const char *port, const char *cmd) {
	int retries = 6;
	unsigned char buf[1024];
	char *rbuf = NULL;
	const int timeout = 0;
	RSocket *s = r_socket_new (0);
	for (;retries>0; r_sys_usleep (10*1000)) {
		if (r_socket_connect (s, host, port, R_SOCKET_PROTO_TCP, timeout))
			break;
		retries--;
	}
	if (retries>0) {
		rbuf = strdup ("");
		r_socket_write (s, (void*)cmd, strlen (cmd));
		//r_socket_write (s, "px\n", 3);
		for (;;) {
			int ret = r_socket_read (s, buf, sizeof (buf));
			if (ret<1) break;
			buf[ret] = 0;
			rbuf = r_str_concat (rbuf, (const char *)buf);
		}
	} else {
		eprintf ("Cannot connect\n");
	}
	r_socket_free (s);
	return rbuf;
}

R_API int r_core_rtr_cmds (RCore *core, const char *port) {
	unsigned char buf[4097];
	RSocket *ch, *s;
	int i, ret;
	char *str;

	if (!port || port[0]=='?') {
		r_cons_printf ("Usage: .:[tcp-port]    run r2 commands for clients\n");
		return R_FALSE;
	}

	s = r_socket_new (0);
	if (!r_socket_listen (s, port, NULL)) {
		eprintf ("Error listening on port %s\n", port);
		r_socket_free (s);
		return R_FALSE;
	}

	eprintf ("Listening for commands on port %s\n", port);
	listenport = port;
	for (;;) {
		r_cons_break ((RConsBreak)http_break, core);
		ch = r_socket_accept (s);
		buf[0] = 0;
		ret = r_socket_read (ch, buf, sizeof (buf) - 1);
		if (ret>0) {
			buf[ret] = 0;
			for (i=0; buf[i]; i++)
				if (buf[i] == '\n')
					buf[i] = buf[i+1]? ';': '\0';
			if (!r_config_get_i (core->config, "scr.prompt") \
					&& !strcmp ((char*)buf, "q!"))
				break;
			str = r_core_cmd_str (core, (const char *)buf);
			if (str &&*str)  {
			r_socket_write (ch, str, strlen (str));
			} else
			r_socket_write (ch, "\n", 1);
			free (str);
		}
		if (r_cons_singleton()->breaked)
			break;
		r_socket_close (ch);
		r_cons_break_end ();
	}
	r_socket_free(s);
	r_socket_free(ch);
	return 0;
}
