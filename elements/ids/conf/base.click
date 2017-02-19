require(package "openbox")
ChatterSocket("TCP", 10002, RETRIES 3, RETRY_WARNINGS false, CHANNEL openbox);
ControlSocket("TCP", 10001, RETRIES 3, RETRY_WARNINGS false);
alert::ChatterMessage("ALERT", "{\"message\": \"This is a test alert\", \"origin_block\": \"alert\", \"packet\": \"00 00 00 00\"}", CHANNEL openbox);
log::ChatterMessage("LOG", "{\"message\": \"This is a test log\", \"origin_block\": \"log\", \"packet\": \"00 00 00 00\"}", CHANNEL openbox);
timed_source::TimedSource(10, "base");
discard::Discard();
timed_source -> alert -> log -> discard;