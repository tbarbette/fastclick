import sys
import os
import json
import dataclasses
import numpy as np
from topologies.mqtt import simpleRun, run_cli
import time

class ExperimentalDesign:

    window_size: int = 10
    window_step: int = 2
    fec_scheme: str = "rlc"

    drop_ge_k = (0.98, 1)
    drop_ge_h = (0, 0.1)
    drop_ge_r = (0, 0.1)
    drop_ge_p = (0, 0.1)

    nb_points: int = 95
    output_dir: str = "test"
    design: str = "wps"
    use_fec: bool = True
    enable_log: bool = False

    # MQTT params
    nb_clients: int = 5
    nb_messages: int = 100  # Per client
    timeout: int = 120  # In ms


    def drop_uniform(self):
        assert self.nb_points % 4 == 0
        for k in np.linspace(self.drop_ge_k[0], self.drop_ge_k[1], int(self.nb_points / 4)):
            for h in np.linspace(self.drop_ge_h[0], self.drop_ge_h[1], int(self.nb_points / 4)):
                for r in np.linspace(self.drop_ge_r[0], self.drop_ge_r[1], int(self.nb_points / 4)):
                    for p in np.linspace(self.drop_ge_p[0], self.drop_ge_p[1], int(self.nb_points / 4)):
                        yield (k, h, r, p)
    
    def drop_wsp(self):
        with open("matrix.txt") as fd:
            lines = fd.readlines()
        lines_split = lines[0].split(",")

        # Construct matrix
        matrix = np.zeros((20, 95))
        for i in range(20):
            for j in range(95):
                matrix[i, j] = lines_split[i * 95 + j]
        
        for i in range(95):
            k = matrix[0, i] * (self.drop_ge_k[1] - self.drop_ge_k[0]) + self.drop_ge_k[0]
            h = matrix[1, i] * (self.drop_ge_h[1] - self.drop_ge_h[0]) + self.drop_ge_h[0]
            r = matrix[2, i] * (self.drop_ge_r[1] - self.drop_ge_r[0]) + self.drop_ge_r[0]
            p = matrix[3, i] * (self.drop_ge_p[1] - self.drop_ge_p[0]) + self.drop_ge_p[0]
            yield (k, h, r, p)

    
    def run(self):
        print("enter")
        net = simpleRun()
        os.makedirs(self.output_dir, exist_ok=True)
        filename = f"test_{self.window_size}_{self.window_step}.json" if self.use_fec else "test_without.json"
        filepath = os.path.join(self.output_dir, filename)

        # Pre set the output file
        os.system("echo [ >> {}".format(filepath))

        mosquitto_msg = "mosquitto"
        if self.enable_log:
            # mosquitto_msg += " > {} 2>&1".format(os.path.join(self.output_dir, "mosquitto.txt"))
            pass
        mosquitto = net["h2"].popen(mosquitto_msg, stdout=open(os.path.join(self.output_dir, "mosquitto1.txt"), "w+"), stderr=open(os.path.join(self.output_dir, "mosquitto2.txt"), "w+"))
        for i, (k, h, r, p) in enumerate(self.drop_wsp()):
            #  if i >= 63 or i < 62: continue
            encoder_msg = "/vagrant/bin/click /vagrant/mqtt_fec/configs/srv6_encode.click window_size={} window_step={} no_fec={}".format(self.window_size, self.window_step, 0 if self.use_fec else 1)
            decoder_msg = "/vagrant/bin/click /vagrant/mqtt_fec/configs/srv6_decode.click drop_k={} drop_h={} drop_p={} drop_r={} no_fec={}".format(str(k), str(h), str(p), str(r), 0 if self.use_fec else 1)
            print(encoder_msg)
            print(decoder_msg)
            # run_cli(net)
            encoder = net["sw1"].popen(encoder_msg, stdout=open(os.path.join(self.output_dir, "encoder1.txt"), "w+"), stderr=open(os.path.join(self.output_dir, "encoder2.txt"), "w+"))
            decoder = net["sw2"].popen(decoder_msg, stdout=open(os.path.join(self.output_dir, "decoder1.txt"), "w+"), stderr=open(os.path.join(self.output_dir, "decoder2.txt"), "w+"))

            encoder_tcpdump = net["h1"].popen("tcpdump -i h1-eth0 -w /vagrant/mqtt_fec/cap.pcap")
            time.sleep(3)
            print("{}: OK with params {} {} {} {}".format(i, k, h, r, p))

            # /vagrant/mqtt_fec/mqtt-benchmark/mqtt-benchmark --broker tcp://[babe:2::5]:1883 --clients 5 --format json --wait 1
            mqtt_msg = "/vagrant/mqtt_fec/mqtt-benchmark/mqtt-benchmark --broker tcp://[babe:2::5]:1883 --clients {} --count {} --format json --all-results --wait 120 >> {}".format(self.nb_clients, self.nb_messages, filepath)
            if self.enable_log:
                mqtt_msg += " >2 {}".format(os.path.join(self.output_dir, "mqtt.txt"))
            print(mqtt_msg)
            print("\n")
            mqtt = net["h1"].cmd(mqtt_msg)

            encoder.terminate()
            decoder.terminate()
            encoder_tcpdump.terminate()

            if i < self.nb_points - 1:
                os.system("echo , >> {}".format(filepath))
        
        os.system("echo ] >> {}".format(filepath))

        net.stop()


def parse(filename: str):
    try:
        with open(filename, "r") as fd:
            json_data = json.load(fd)
    except json.JSONDecodeError as e:
        print(f"File ({filename}) not correctly formatted: {e}", file=sys.stderr)

    exp = ExperimentalDesign()

    exp.nb_points = json_data["nb_points"]
    exp.output_dir = json_data["output"]
    exp.design = json_data["design"]
    exp.use_fec = json_data["use_fec"]
    exp.enable_log = json_data.get("enable_log", exp.enable_log)

    # Drop model
    decoder_info = json_data["decoder"]
    if "ge" in decoder_info.keys():
        ge_info = decoder_info["ge"]
        exp.drop_ge_k = (ge_info["k"]["min"], ge_info["k"]["max"])
        exp.drop_ge_p = (ge_info["p"]["min"], ge_info["p"]["max"])
        exp.drop_ge_h = (ge_info["h"]["min"], ge_info["h"]["max"])
        exp.drop_ge_r = (ge_info["r"]["min"], ge_info["r"]["max"])
    else:
        raise WrongDropModelException

    # FEC Scheme
    encoder_info = json_data["encoder"]
    # TODO: use scheme also
    exp.window_size = encoder_info["window_size"]
    exp.window_step = encoder_info["window_step"]

    # MQTT parameters
    mqtt_info = json_data.get("mqtt", {})
    exp.nb_clients = mqtt_info.get("nb_clients", exp.nb_clients)
    exp.nb_messages = mqtt_info.get("nb_messages", exp.nb_messages)
    exp.timeout = mqtt_info.get("timeout", exp.timeout)

    return exp


class WrongDropModelException(Exception):
    pass


if __name__ == "__main__":
    args = sys.argv
    print("kzkzkkzkkzkz")
    if len(args) != 2:
        print("Usage: mqtt.py config_path", file=sys.stderr)
        sys.exit(1)
    exp = parse(args[1])
    exp.run()