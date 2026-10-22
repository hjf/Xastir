# Xastir Qt migration

This repository is undergoing an incremental migration from the classic
X11/Motif (formerly LessTif, now OpenMotif) frontend to a modern Qt 6 frontend.
The goal is a responsive, cross-platform APRS application with behavior grounded
in the original Xastir implementation. The migration work is on branch `qt`.

## Implementation criteria

- Reuse original C business logic where practical. Extract it from UI-dependent
  files, remove Motif/X11 coupling, and compile it into the shared `xastir_core`
  library. Qt should call the extracted routines rather than duplicate their
  behavior in C++.
- Keep protocol parsing, station storage, message delivery/retry state, packet
  formatting, and reusable calculations independent of the frontend. Pass
  dependencies explicitly instead of retaining UI globals or dialog callbacks.
- Implement presentation and platform adapters with Qt: windows, menus,
  dialogs, map painting, user interaction, sockets, and storage integration.
  Replacing X11 drawing or UI code with Qt implementations is expected.
- When legacy logic is intertwined with UI, refactor that boundary as part of
  the feature. The user explicitly accepts breaking the old Motif frontend as
  UI plumbing is removed; maintaining that frontend must not prevent extraction.
  Preserve its business behavior where applicable.
- Earlier MVP code includes new Qt implementations. Do not assume every
  component already shares original C logic; inspect it before extending it.
- Deliver usable, incremental features. Follow the original menu structure,
  clearly marking unfinished actions. New capabilities such as persistent
  history are welcome when they solve a concrete user problem.

## Scope and behavior

- The established MVP includes a main window, OSM raster map, APRS station,
  object and item rendering, messaging, and station/interface controls.
- Radio/TNC integration supports AGWPE and serial standard KISS. Serial GPS,
  command-mode TNCs and proprietary KISS variants remain outside this iteration.
  APRS-IS is a separate Internet transport. Optional KISS mode entry sends a
  configured sequence on open/reconnect before allowing KISS traffic; it must
  remain opt-in. TNC parameter writes require an explicit user action.
- Preserve explicit transmit controls. Receive-only interfaces remain
  receive-only until enabled. Disable Transmit ALL applies to messages, ACKs,
  and position beacons. Position transmission has its own control.
- APRS-IS messaging requires enabled TX and server-confirmed verified login.
  Internet replies must not silently reuse an RF route. Reception must not
  introduce an implicit RF/Internet gateway.
- History stores traffic for seven days with additional count/size limits;
  messages are retained indefinitely. Startup restoration preserves original
  timestamps and updates station state without transmitting, ACKing, or
  auto-replying. Interrupted outgoing messages require manual retry.
- Keep the map and UI responsive. Bound traffic buffers and work batches,
  and run database writes off the UI thread.

## Validation and workflow

- Validate protocol and C-core changes with meaningful fixtures and existing
  compatibility tests. Test actual wire formats, delivery transitions, replay
  safety, and relevant UI interactions rather than only mirroring implementation.
- Build and test on Linux during the current work. A Windows test build has
  previously worked, but the user explicitly requested **no further Windows
  builds in the meantime**. Preserve cross-platform support; resume Windows
  builds only when requested.
- The user may be working remotely. Do not launch the interactive application
  on their desktop without coordinating first. Headless/offscreen tests are fine.
- Commit completed changes as work progresses. Stage relevant files explicitly;
  leave unrelated user edits, generated files, build outputs, and local
  configuration out of commits. Do not assume a commit request also requests a
  push.
- Report what changed, how it was verified, and material limitations. Distinguish
  packets queued to a TNC, local TX loopback, actual RF reception, digipeater
  repetition, and message ACKs when diagnosing radio behavior.
- External TNC/digipeater sources and live devices are separate from this
  repository. Use session-authorized access and keep diagnostics read-only
  unless a configuration or firmware change has been requested.
