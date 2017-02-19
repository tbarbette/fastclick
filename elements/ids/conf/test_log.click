ChatterSocket("TCP", 7001, RETRIES 3, RETRY_WARNINGS false, CHANNEL openbox);
ControlSocket(TCP, 9000, RETRIES 3, RETRY_WARNINGS false);
require(package "openbox");
chater_msg::PushMessage("LOG", "This is a message: %s", CHANNEL openbox, ATTACH_PACKET rue);

TimedSource(1, "blabla") 
-> chater_msg 
-> Discard();

