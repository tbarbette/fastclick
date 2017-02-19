ChatterSocket("TCP", 7001, RETRIES 3, RETRY_WARNINGS false, CHANNEL test);
ControlSocket(TCP, 9000, RETRIES 3, RETRY_WARNINGS false);
TimedSource(1, "blabla") 
-> Print()
-> Discard();