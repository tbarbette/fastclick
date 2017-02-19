require(package "openbox");
ControlSocket(TCP, 9000, RETRIES 3, RETRY_WARNINGS false);
mc::MultiCounter();
TimedSource(0.1, "blabla") -> mc ->Print -> Discard;
TimedSource(0.5, "qwer") -> [1]mc[1] -> Print ->Discard;
