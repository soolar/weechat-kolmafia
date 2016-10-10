#ifndef WEECHAT_KOLMAFIA_H
#define WEECHAT_KOLMAFIA_H

#include "weechat-plugin.h"
#include "weechat-kolmafia-config.h"
#include "json/json.h"
#include <random>
#include <map>

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
      static int input_channel_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *input_data);
      static int input_whisper_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *input_data);
      static int input_cli_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *input_data);
      static int close_channel_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf);
      static int close_whisper_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf);
      static int close_cli_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf);
      static int poll_callback(const void *ptr, void *data, int remaining_calls);
      static int poll_cli_callback(const void *ptr, void *data, int remaining_calls);
      static int update_nicklists_callback(const void *ptr, void *data, int remaining_calls);

      // commands
#define COMMAND_DECLARATION(CMD) static int CMD##_command_aux(const void *ptr, void *data, struct t_gui_buffer *weebuf, int argc, char **argv, char **argv_eol); int CMD##_command(struct t_gui_buffer *weebuf, int argc, char **argv, char **arv_eol);
      COMMAND_DECLARATION(me)
      COMMAND_DECLARATION(who)

    private:
      struct t_weechat_plugin *weechat_plugin;
      std::string lastSeen;
      struct t_gui_buffer *dbg;
      struct t_gui_buffer *events;
      struct t_gui_buffer *cli;
      config conf;
      std::map<std::string, std::string> name_deuniquifies;
      std::map<std::string, struct t_gui_buffer *> channels;
      std::map<std::string, struct t_gui_buffer *> whispers;

      int http_request(const std::string &url, std::string &outbuf);
      void handle_message(const Json::Value &msg);

      void update_session();

      std::string url_encode(const std::string &text);
      std::string name_uniquify(const std::string &name);
      std::string name_deuniquify(const std::string &name);
      std::string html_to_weechat(const std::string &html);
      int submit_message(const std::string &message, std::string &outbuf);
      int handle_input_channel(struct t_gui_buffer *weebuf, const char *input_data);
      int handle_input_whisper(struct t_gui_buffer *weebuf, const char *input_data);
      int handle_input_cli(struct t_gui_buffer *weebuf, const char *input_data);
      int handle_close_channel(struct t_gui_buffer *weebuf);
      int handle_close_whisper(struct t_gui_buffer *weebuf);
      int handle_close_cli(struct t_gui_buffer *weebuf);
      int poll_messages();
      int poll_cli_messages();

      int set_poll_delay(long newdelay);

      void update_nicklist(struct t_gui_buffer *weebuf);
      int update_all_nicklists();
      struct t_gui_buffer *get_channel_buffer(const std::string &channel);
      struct t_gui_buffer *get_whisper_buffer(const std::string &name);
      struct t_hook *poll_hook;
      struct t_hook *poll_cli_hook;
      struct t_hook *update_nicklists_hook;
      long delay;

      std::default_random_engine generator;
      std::uniform_real_distribution<double> distribution;

      bool be_good; // safety flag, set to false in the destructor, NEVER TOUCH OTHERWISE
    };
}

#endif // WEECHAT_KOLMAFIA_H

