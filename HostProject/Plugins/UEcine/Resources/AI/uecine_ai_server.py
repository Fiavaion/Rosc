"""UEcine AI helper: runs UEcine's picture models outside the Unreal editor.

The UEcine panel starts this on the same machine ("Helper on this machine"), or a studio runs
it on another machine and gives the panel its address ("Server elsewhere"). It speaks the
protocol uecine-ai/1:

    GET    /v1/health               what this server can do
    POST   /v1/jobs?task=<task>     body: the picture as a PNG; answers {"id": ...}
    GET    /v1/jobs/<id>            state, stage, fraction, and the result when done
    GET    /v1/jobs/<id>/picture    the resulting picture as a PNG (depth and matte)
    DELETE /v1/jobs/<id>            give a job up

Tasks: depth (16-bit grey PNG, near is bright), matte (8-bit grey PNG), focal (the horizontal
field of view in degrees). Needs Python 3.9 or later with: onnxruntime numpy pillow.
Model files are not part of UEcine: they are the files the AI tab downloads, in the folder
given with --models-folder.
"""

import argparse
import hmac
import io
import json
import math
import os
import queue
import ssl
import sys
import threading
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

PROTOCOL = "uecine-ai/1"
MAX_PICTURE_BYTES = 64 * 1024 * 1024
JOBS_KEPT = 16

try:
    import numpy as np
    import onnxruntime as ort
    from PIL import Image
except ImportError as error:
    # The panel shows this line to the user.
    print(f"UECINE-AI MISSING {error.name}", file=sys.stderr, flush=True)
    sys.exit(3)


def input_size(recipe, width, height):
    """The size a picture is given to a model at."""
    if "width" in recipe:
        return recipe["width"], recipe["height"]
    scale = recipe["short_side"] / min(width, height)
    multiple = recipe["multiple_of"]
    return (max(multiple, round(width * scale / multiple) * multiple),
            max(multiple, round(height * scale / multiple) * multiple))


def picture_tensor(picture, recipe):
    width, height = input_size(recipe, picture.width, picture.height)
    pixels = np.asarray(picture.resize((width, height), Image.BICUBIC), dtype=np.float32) / 255.0
    pixels = (pixels - np.array(recipe["mean"], dtype=np.float32)) / np.array(recipe["std"], dtype=np.float32)
    return pixels.transpose(2, 0, 1)[None]


def resized(values, width, height):
    """A grid of numbers at another size."""
    return np.asarray(Image.fromarray(values.astype(np.float32), mode="F").resize((width, height), Image.BILINEAR))


def field_of_view(points, mask):
    """The horizontal field of view, in degrees, of the camera that saw a map of points.

    The points are right up to a scale and a shift along the lens axis. For each shift the focal
    length that best projects the points onto the picture is known at once, so the shift is
    searched for (after MoGe, Wang et al.).
    """
    height, width = mask.shape
    aspect = width / height
    span_x = aspect / math.hypot(1.0, aspect)
    span_y = 1.0 / math.hypot(1.0, aspect)
    rows = np.linspace(0, height - 1, min(height, 64)).round().astype(int)
    columns = np.linspace(0, width - 1, min(width, 64)).round().astype(int)
    u = (2.0 * (columns + 0.5) / width - 1.0) * span_x
    v = (2.0 * (rows + 0.5) / height - 1.0) * span_y
    uv = np.stack(np.meshgrid(u, v), axis=-1)
    points = points[rows][:, columns]
    seen = mask[rows][:, columns] > 0.5
    if seen.sum() < 2:
        return None
    uv, xy, z = uv[seen].astype(np.float64), points[seen][:, :2].astype(np.float64), points[seen][:, 2].astype(np.float64)

    def focal_and_error(shift):
        projected = xy / (z + shift)[:, None]
        focal = (projected * uv).sum() / np.square(projected).sum()
        return focal, np.square(focal * projected - uv).sum()

    # Every point has to stay in front of the camera.
    low = -z.min() + 1e-3 * (z.max() - z.min() + 1e-6)
    high = low + 4.0 * (z.max() - z.min() + 1e-6) + 4.0 * abs(z.max())
    ratio = (math.sqrt(5.0) - 1.0) / 2.0
    for _ in range(60):
        a = high - ratio * (high - low)
        b = low + ratio * (high - low)
        if focal_and_error(a)[1] < focal_and_error(b)[1]:
            high = b
        else:
            low = a
    focal = focal_and_error((low + high) / 2.0)[0]
    if not math.isfinite(focal) or focal <= 0.0:
        return None
    return math.degrees(2.0 * math.atan(span_x / focal))


class Models:
    """The models of UEcine's list whose files are in the models folder."""

    def __init__(self, list_file, folder):
        with open(list_file, encoding="utf-8") as file:
            listed = json.load(file)["models"]
        self.folder = folder
        self.by_task = {}
        for model in listed:
            path = folder / model["file"]
            if path.is_file() and model["task"] not in self.by_task:
                self.by_task[model["task"]] = model
        self.sessions = {}

    def session(self, model):
        if model["id"] not in self.sessions:
            # One model in memory at a time: they are a few hundred megabytes each.
            self.sessions.clear()
            wanted = ("CUDAExecutionProvider", "DmlExecutionProvider", "CPUExecutionProvider")
            providers = [name for name in wanted if name in ort.get_available_providers()]
            self.sessions[model["id"]] = ort.InferenceSession(str(self.folder / model["file"]), providers=providers)
        return self.sessions[model["id"]]

    def run(self, task, picture, say):
        model = self.by_task[task]
        recipe = model["input"]
        recipe = {
            "mean": recipe["mean"], "std": recipe["std"],
            **({"width": recipe["width"], "height": recipe["height"]} if "width" in recipe
               else {"short_side": recipe["shortSide"], "multiple_of": recipe["multipleOf"]}),
        }
        say("Loading the model", 0.1)
        session = self.session(model)
        say("Working on the picture", 0.4)
        feeds = {model["input"]["name"]: picture_tensor(picture, recipe)}
        for name, value in model.get("integerInputs", {}).items():
            feeds[name] = np.array(value, dtype=np.int64)
        names = [model["output"]] + ([model["maskOutput"]] if "maskOutput" in model else [])
        outputs = session.run(names, feeds)
        say("Finishing", 0.9)
        if task == "depth":
            depth = resized(np.squeeze(outputs[0]), picture.width, picture.height)
            low, high = float(depth.min()), float(depth.max())
            depth = (depth - low) / max(high - low, 1e-12)
            return {"picture": Image.fromarray((depth * 65535.0).round().astype(np.uint16))}
        if task == "matte":
            matte = 1.0 / (1.0 + np.exp(-resized(np.squeeze(outputs[0]), picture.width, picture.height)))
            return {"picture": Image.fromarray((matte * 255.0).round().astype(np.uint8))}
        degrees = field_of_view(np.squeeze(outputs[0]), np.squeeze(outputs[1]))
        if degrees is None:
            raise ValueError("The picture has too little in it to judge the lens from.")
        return {"fovDegrees": degrees}


class Jobs:
    """Runs jobs one at a time, in the order they came."""

    def __init__(self, models):
        self.models = models
        self.lock = threading.Lock()
        self.jobs = {}
        self.order = []
        self.waiting = queue.Queue()
        threading.Thread(target=self.work, daemon=True).start()

    def add(self, task, picture_bytes):
        job_id = uuid.uuid4().hex
        job = {"state": "queued", "stage": "Waiting", "fraction": 0.0, "task": task, "bytes": picture_bytes}
        with self.lock:
            self.jobs[job_id] = job
            self.order.append(job_id)
            while len(self.order) > JOBS_KEPT:
                self.jobs.pop(self.order.pop(0), None)
        self.waiting.put(job_id)
        return job_id

    def get(self, job_id):
        with self.lock:
            return self.jobs.get(job_id)

    def cancel(self, job_id):
        with self.lock:
            job = self.jobs.get(job_id)
            if job is None:
                return False
            if job["state"] in ("queued", "running"):
                job["state"] = "cancelled"
            job.pop("bytes", None)
            job.pop("result", None)
            return True

    def work(self):
        while True:
            job = self.get(self.waiting.get())
            if job is None:
                continue
            with self.lock:
                if job["state"] != "queued":
                    continue
                job["state"] = "running"
                picture_bytes = job.pop("bytes")

            def say(stage, fraction, job=job):
                with self.lock:
                    job["stage"], job["fraction"] = stage, fraction

            try:
                say("Reading the picture", 0.05)
                picture = Image.open(io.BytesIO(picture_bytes)).convert("RGB")
                result = self.models.run(job["task"], picture, say)
                with self.lock:
                    if job["state"] == "running":
                        job.update(state="done", stage="Done", fraction=1.0, result=result)
            except Exception as error:  # A job that fails must not take the server down.
                with self.lock:
                    if job["state"] == "running":
                        job.update(state="failed", error=str(error))


class Handler(BaseHTTPRequestHandler):
    jobs = None
    key = ""

    def log_message(self, format, *args):
        pass

    def answer(self, status, body, content_type="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def refuse(self, status, message):
        self.answer(status, {"error": message})

    def allowed(self):
        if not self.key or hmac.compare_digest(self.headers.get("Authorization", ""), "Bearer " + self.key):
            return True
        self.refuse(401, "This server needs its key.")
        return False

    def job_path(self):
        """The job id and what is asked of it, for a path under /v1/jobs/."""
        parts = urlparse(self.path).path.strip("/").split("/")
        if len(parts) in (3, 4) and parts[:2] == ["v1", "jobs"]:
            return parts[2], (parts[3] if len(parts) == 4 else "")
        return None, None

    def do_GET(self):
        if not self.allowed():
            return
        if urlparse(self.path).path == "/v1/health":
            self.answer(200, {"protocol": PROTOCOL, "tasks": sorted(self.jobs.models.by_task)})
            return
        job_id, what = self.job_path()
        job = self.jobs.get(job_id) if job_id else None
        if job is None:
            self.refuse(404, "There is no such job.")
        elif what == "":
            status = {name: job[name] for name in ("state", "stage", "fraction", "error") if name in job}
            if job["state"] == "done":
                result = job["result"]
                status["result"] = ({"fovDegrees": result["fovDegrees"]} if "fovDegrees" in result
                                    else {"picture": f"/v1/jobs/{job_id}/picture"})
            self.answer(200, status)
        elif what == "picture" and job["state"] == "done" and "picture" in job.get("result", {}):
            png = io.BytesIO()
            job["result"]["picture"].save(png, format="PNG")
            self.answer(200, png.getvalue(), "image/png")
        else:
            self.refuse(404, "This job has no picture.")

    def do_POST(self):
        if not self.allowed():
            return
        address = urlparse(self.path)
        if address.path != "/v1/jobs":
            self.refuse(404, "Not found.")
            return
        task = parse_qs(address.query).get("task", [""])[0]
        if task not in self.jobs.models.by_task:
            self.refuse(400, f"This server has no model for '{task}'.")
            return
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0 or length > MAX_PICTURE_BYTES:
            self.refuse(413, "The picture is missing or too large.")
            return
        self.answer(200, {"id": self.jobs.add(task, self.rfile.read(length))})

    def do_DELETE(self):
        if not self.allowed():
            return
        job_id, what = self.job_path()
        if job_id and what == "" and self.jobs.cancel(job_id):
            self.answer(200, {})
        else:
            self.refuse(404, "There is no such job.")


def main():
    from pathlib import Path

    parser = argparse.ArgumentParser(description="UEcine AI helper")
    parser.add_argument("--models-folder", required=True, type=Path, help="where the model files are")
    parser.add_argument("--models-list", required=True, type=Path, help="UEcine's models.json")
    parser.add_argument("--host", default="127.0.0.1", help="the address to listen on (default: this machine only)")
    parser.add_argument("--port", default=0, type=int, help="the port to listen on (default: any free port)")
    parser.add_argument("--key", default="", help="the key callers must give")
    parser.add_argument("--stop-with-input", action="store_true",
                        help="stop when standard input closes, as it does when the program that started this goes away")
    parser.add_argument("--certificate", type=Path, help="serve over https with this certificate (PEM)")
    parser.add_argument("--certificate-key", type=Path, help="the certificate's private key (PEM)")
    arguments = parser.parse_args()

    if arguments.host not in ("127.0.0.1", "localhost", "::1") and not arguments.key:
        parser.error("a --key is needed to listen beyond this machine")

    Handler.jobs = Jobs(Models(arguments.models_list, arguments.models_folder))
    Handler.key = arguments.key
    server = ThreadingHTTPServer((arguments.host, arguments.port), Handler)
    scheme = "http"
    if arguments.certificate:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(arguments.certificate, arguments.certificate_key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        scheme = "https"
    if arguments.stop_with_input:
        def stop_when_input_closes():
            sys.stdin.buffer.read()
            os._exit(0)
        threading.Thread(target=stop_when_input_closes, daemon=True).start()
    # The panel waits for this line.
    print(f"UECINE-AI READY {scheme}://{arguments.host}:{server.server_address[1]}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
