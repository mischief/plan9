# avi

## converting videos

Convert a video from NASA into a format avi can play:

- `-sn`: no subs
- `-ac 2 -acodec pcm_s16le -ar 44100`: 2 channel 44.1khz signed 16 bit little-endian pcm
- `-vcodec mjpeg -q:v 2 -r 15`: motion jpeg, high quality, 15 fps

	ffmpeg -hide_banner -loglevel -8 -stats -y \
		-i http://s3.amazonaws.com/akamai.netstorage/HD_downloads/rbsp_launch_480p.mp4 \
		-sn \
		-ac 2 -acodec pcm_s16le -ar 44100 \
		-vcodec mjpeg -q:v 2 -r 15 \
		liftoff.avi

