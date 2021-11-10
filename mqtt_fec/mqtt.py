import sys
import os
import json
import dataclasses
import numpy as np
from topologies.mqtt import simpleRun, run_cli
import time

class ExperimentalDesign:

    window_size: list = [10]
    window_step: list = [2]
    fec_scheme: str = "rlc"

    drop_ge_k = (0.9, 1)
    drop_ge_h = (0, 0.1)
    drop_ge_r = (0, 0.1)
    drop_ge_p = (0, 0.1)

    nb_points: int = 100
    output_dir: str = "test"
    design: str = "wps"

    def drop_uniform(self):
        assert self.nb_points % 4 == 0
        for k in np.linspace(self.drop_ge_k[0], self.drop_ge_k[1], int(self.nb_points / 4)):
            for h in np.linspace(self.drop_ge_h[0], self.drop_ge_h[1], int(self.nb_points / 4)):
                for r in np.linspace(self.drop_ge_r[0], self.drop_ge_r[1], int(self.nb_points / 4)):
                    for p in np.linspace(self.drop_ge_p[0], self.drop_ge_p[1], int(self.nb_points / 4)):
                        yield (k, h, r, p)
    

    def run(self):
        print("enter")
        net = simpleRun()
        # os.mkdir(self.output_dir)

        mosquitto = net["h2"].popen("mosquitto")
        for i, (k, h, r, p) in enumerate(self.drop_uniform()):
            encoder = net["sw1"].popen(f"/vagrant/bin/click /vagrant/mqtt_fec/configs/srv6_encode.click")
            decoder = net["sw2"].popen(f"/vagrant/bin/click /vagrant/mqtt_fec/configs/srv6_decode.click")
            time.sleep(3)
            print("OK")

            net["h1"].cmd(f"/home/vagrant/go/bin/mqtt-benchmark --broker tcp://[babe:2::5]:1883 --clients 3 >> test_file.json 2>&1")

            encoder.terminate()
            decoder.terminate()
            #print(encoder)
            print("couocukeofkeo")
            

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

    # TODO: parse the remaining information from the JSON

    return exp


if __name__ == "__main__":
    args = sys.argv
    print("kzkzkkzkkzkz")
    if len(args) != 2:
        print("Usage: mqtt.py config_path", file=sys.stderr)
        sys.exit(1)
    exp = parse(args[1])
    exp.run()