ChatterSocket("TCP", 7001, RETRIES 3, RETRY_WARNINGS false, CHANNEL test);
ControlSocket(TCP, 9000, RETRIES 3, RETRY_WARNINGS false);
require(package "openbox");
chater_msg::ChatterMessage("LOG", "{\"type\": \"log\", \"message\": \"The is a log message\", \"sevirity\": 0, \"origin_dpid\": 123, \"origin_block\": \"block1\"}", CHANNEL test);

TimedSource(1, "blabla") 
-> chater_msg
-> Discard();
