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

# avirec

it's a good idea to write these into a tmpfs in case your real fs is remote or otherwise slow.

## usb camera

	nusb/cam 9
	avirec -p /shr/usb/cam9.1/video /tmp/rec.avi

## record a rio window

	avirec -p /dev/wsys/10/window /tmp/rec.avi

## record the whole screen

	avirec -p /dev/screen /tmp/rec.avi
