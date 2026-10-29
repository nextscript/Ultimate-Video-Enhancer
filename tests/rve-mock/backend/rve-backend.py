# Test double for REAL-Video-Enhancer's rve-backend.py (used by `electron . --selftest` only).
# Same command line as the real backend; the read/write FFmpeg commands mirror RVE's
# backend/src/FFmpegBuffers.py (rgb24 image2pipe in, rawvideo + original file -> custom encoder out).
# Instead of a neural network the "model" is a 2x Lanczos resize so the pipeline can be verified
# without the multi-GB TensorRT stack.
import argparse, json, subprocess, sys, time

p = argparse.ArgumentParser()
for a in ['--input', '--output', '--backend', '--upscale_model', '--precision', '--ffmpeg_path', '--custom_encoder',
          '--audio_encoder_preset', '--subtitle_encoder_preset']:
    p.add_argument(a, type=str, default=None)
p.add_argument('--tensorrt_opt_profile', type=int, default=3)
p.add_argument('--overwrite', action='store_true')
p.add_argument('--version', action='store_true')
args = p.parse_args()
if args.version:
    print('mock-1.0')
    sys.exit(0)
assert args.backend == 'tensorrt', 'expected tensorrt backend'
scale = 2
ff = args.ffmpeg_path
import os
ffprobe = os.path.join(os.path.dirname(ff), 'ffprobe.exe' if ff.lower().endswith('.exe') else 'ffprobe')
probe = subprocess.run([ffprobe, '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'stream=width,height,r_frame_rate',
                        '-of', 'json', args.input], capture_output=True, text=True)
st = json.loads(probe.stdout)['streams'][0]
w, h = int(st['width']), int(st['height'])
num, den = [int(x) for x in st['r_frame_rate'].split('/')]
fps = num / den
print(f'Loading TensorRT engine for {args.upscale_model}.', file=sys.stderr)

read = subprocess.Popen([ff, '-i', args.input, '-vf', f'crop={w}:{h}:0:0,scale=w=iw*sar:h=ih', '-f', 'image2pipe',
                         '-pix_fmt', 'rgb24', '-vcodec', 'rawvideo', '-s', f'{w}x{h}', '-'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
up = subprocess.Popen([ff, '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{w}x{h}', '-i', '-',
                       '-vf', f'scale={w*scale}:{h*scale}:flags=lanczos', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'],
                      stdin=subprocess.PIPE, stdout=subprocess.PIPE)
cmd = [ff, '-loglevel', 'error', '-framerate', f'{fps}', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-vcodec', 'rawvideo',
       '-s', f'{w*scale}x{h*scale}', '-i', '-', '-r', f'{fps}',
       '-fflags', '+genpts', '-i', args.input, '-map', '0:v', '-map', '1:a?', '-map', '1:s?',
       '-avoid_negative_ts', 'make_zero', '-max_interleave_delta', '0', '-muxpreload', '0', '-muxdelay', '0']
cmd += args.custom_encoder.split()
cmd += [args.output]
if args.overwrite:
    cmd.append('-y')
write = subprocess.Popen(cmd, stdin=subprocess.PIPE)

fb_in, fb_out = w * h * 3, w * h * 3 * scale * scale
frames, t0 = 0, time.time()
import threading
def pump_out():
    global frames
    while True:
        d = up.stdout.read(fb_out)
        if not d or len(d) < fb_out:
            break
        write.stdin.write(d)
        frames += 1
        if frames % 5 == 0:
            el = max(time.time() - t0, 1e-3)
            sys.stdout.write(f'\rFPS: {round(frames / el)} Current Frame: {frames} ETA: 0:0:0')
            sys.stdout.flush()
    write.stdin.close()
th = threading.Thread(target=pump_out)
th.start()
while True:
    d = read.stdout.read(fb_in)
    if not d or len(d) < fb_in:
        break
    up.stdin.write(d)
up.stdin.close()
th.join()
code = write.wait()
sys.stdout.write(f'\rFPS: {round(frames / max(time.time() - t0, 1e-3))} Current Frame: {frames} ETA: 0:0:0\n')
sys.exit(code)
