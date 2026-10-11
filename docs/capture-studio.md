# Falcon Capture Studio (first-party experimental screen recorder)

Falcon Capture Studio is an original FalconOS C app, not OBS or a
pre-rendered mockup. Open Launchpad -> Capture Studio or find it with search.

- Space / R / on-screen Record: start or stop bounded true screen capture.
- S: screenshot request, P: replay or pause RAM captures.
- Left/Right: seek RAM replay frames with recording stopped.
- Ctrl+Shift+R: global start/stop, including while Falco is focused.
- Ctrl+Shift+P: global screenshot event.
- The live preview and RAM replay are pulled directly from the native
  FalconOS framebuffer after gfx_present(), quantized to RGB565,
  192x108, max 96 frames (24 seconds at four frames per second).
- An optional QEMU companion receives first-party FCAP/1 START, SNAP
  and STOP events through a virtual debug port and captures the actual
  guest RFB/VNC display. Video is encoded as true 1280x720 H.264 MP4
  with FFmpeg. No OBS, no fake screenshots or recorded browser web page.
- Current persistent XFS allows only 32 KiB per file. On real hardware,
  captures stay in RAM and vanish at reboot. The QEMU-only companion
  exports large files on the host. No audio track or hardware codec.
  FFmpeg handles encoding, but the capture app, preview and controller
  are our own software.

## Build and test

CI: .github/workflows/capture-studio-real-qemu.yml

Boots a real FalconOS guest, opens Capture Studio, starts recording,
opens https://falconos.tech/ in Falco, navigates Features and Architecture,
stops recording, validates real MP4 with ffprobe, uploads PNG + MP4.
