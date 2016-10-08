#ifndef WEECHAT_KOLMAFIA_H
#define WEECHAT_KOLMAFIA_H

#include "weechat-plugin.h"
#include "weechat-kolmafia-config.h"
#include "json/json.h"
#include <random>

extern "C"
{
  extern int weechat_plugin_init(struct t_weechat_plugin *plugin, int argc, char *argv[]);
  extern int weechat_plugin_end(struct t_weechat_plugin *plugin);
}

namespace weechat_kolmafia
{
  class plugin
  {
    public:
      plugin(struct t_weechat_plugin *plug);
      ~plugin();

      // callbacks
      static int input_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *input_data);
      static int close_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf);
      static int poll_callback(const void *ptr, void *data, int remaining_calls);

    private:
      struct t_weechat_plugin *weechat_plugin;
      std::string lastSeen;
      struct t_gui_buffer *dbg;
      config conf;

      int http_request(const std::string &url, std::string &outbuf);
      void handle_message(const Json::Value &msg);

      void update_session();

      int handle_input(struct t_gui_buffer *weebuf, const char *input_data);
      int handle_close(struct t_gui_buffer *weebuf);
      int poll_messages();

      int set_poll_delay(long newdelay);

      struct t_gui_buffer *get_channel_buffer(const char *channel);
      struct t_hook *poll_hook;
      long delay;

      std::default_random_engine generator;
      std::uniform_real_distribution<double> distribution;
    };
}

#endif // WEECHAT_KOLMAFIA_H

