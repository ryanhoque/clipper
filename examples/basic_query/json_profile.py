from __future__ import print_function
import os, sys
cur_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.abspath('%s/../clipper_admin' % cur_dir))
from clipper_admin import ClipperConnection, DockerContainerManager
from clipper_admin.deployers import python as python_deployer
import json
import requests
from datetime import datetime
import time
import numpy as np
import signal
import sys


def predict(addr, x, batch=False):
    url = "http://%s/json/predict" % addr

    if batch:
        req_json = json.dumps({'input_batch': x})
    else:
        req_json = json.dumps({'input': list(x)})

    headers = {'Content-type': 'application/json'}
    start = datetime.now()
    r = requests.post(url, headers=headers, data=req_json)
    end = datetime.now()
    latency = (end - start).total_seconds() * 1000.0
    print("'%s', %f ms" % (r.text, latency))


def feature_sum(xs):
    return [str(sum(x)) for x in xs]


# Stop Clipper on Ctrl-C
def signal_handler(signal, frame):
    print("Stopping Clipper...")
    clipper_conn = ClipperConnection(DockerContainerManager())
    clipper_conn.stop_all()
    sys.exit(0)


if __name__ == '__main__':
    signal.signal(signal.SIGINT, signal_handler)
    clipper_conn = ClipperConnection(DockerContainerManager())
    batch_size = int(sys.argv[2])
    image_size = int(sys.argv[1])
    #clipper_conn.start_clipper()
    clipper_conn.connect()
    # run below if first time
    #clipper_conn.register_application(name="json", input_type="floats", default_output="-1.0", slo_micros=100000)
    time.sleep(2)

    try:
        if batch_size > 1:
            for _ in range(500):
                predict(
                    clipper_conn.get_query_addr(),
                    [list(np.random.random(image_size * image_size)) for i in range(batch_size)],
                    batch=True)
        else:
            for _ in range(500):
                predict(clipper_conn.get_query_addr(), np.random.random(image_size * image_size))
        metrics = clipper_conn.inspect_instance()
        print("METRICS ", metrics)
        fh = open('profile_output.json', 'a')
        fh.write("image size: " + str(image_size) + " and batch size: " + str(batch_size) + '\n')
        fh.write(str(metrics) + '\n')
        fh.close()
        clipper_conn.stop_all()
    except Exception as e:
        print(e)
        clipper_conn.stop_all()
