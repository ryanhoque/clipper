from __future__ import print_function
import os, sys
cur_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.abspath('%s/../../clipper_admin' % cur_dir))
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
    url = "http://%s/simple-example/predict" % addr

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
    clipper_conn.start_clipper()
    clipper_conn.register_application(name="simple-example", input_type="doubles", default_output="-1.0", slo_micros=100000)
    time.sleep(2)
    python_deployer.deploy_python_closure(clipper_conn, name="simple-example", version=1, input_type="doubles",
                                    func=feature_sum)
    time.sleep(2)
    clipper_conn.link_model_to_app(app_name="simple-example", model_name="simple-example")
    # For batch inputs set this number > 1
    batch_size = 1

    try:
        while True:
            if batch_size > 1:
                predict(
                    clipper_conn.get_query_addr(),
                    [list(np.random.random(200)) for i in range(batch_size)],
                    batch=True)
            else:
                predict(clipper_conn.get_query_addr(), np.random.random(200))
            time.sleep(0.2)
    except Exception as e:
        clipper_conn.stop_all()
