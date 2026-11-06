project(
  'hostchat',
  'c',
  version: '0.1.0',
  meson_version: '>=0.61.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'buildtype=debugoptimized',
  ],
)

add_project_arguments('-D_DEFAULT_SOURCE', language: 'c')

serve_inc = include_directories('Code/Serve')
gui_inc = include_directories('Code/Gui')

hostchat_serve = static_library(
  'hostchat_serve',
  files(
    'Code/Serve/Serve.c',
    'Code/Serve/Relay.c',
    'Code/Serve/RelaySession.c',
  ),
  include_directories: serve_inc,
)

hostchat_serve_dep = declare_dependency(
  link_with: hostchat_serve,
  include_directories: serve_inc,
)

executable(
  'chatd',
  files('Code/Cli/ChatDaemon.c'),
  dependencies: hostchat_serve_dep,
  install: false,
)

executable(
  'chat-serve',
  files('Code/Cli/ChatServe.c'),
  dependencies: hostchat_serve_dep,
  install: false,
)

gtk4_dep = dependency('gtk4')

executable(
  'hostchat-gui',
  files(
    'Code/Gui/Main.c',
    'Code/Gui/Layout.c',
    'Code/Gui/Worker.c',
    'Code/Gui/Client.c',
    'Code/Gui/Protocol.c',
    'Code/Gui/Conversation.c',
  ),
  include_directories: [serve_inc, gui_inc],
  dependencies: [hostchat_serve_dep, gtk4_dep],
  install: false,
)
