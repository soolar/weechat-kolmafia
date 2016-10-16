#ifndef WEECHAT_KOLMAFIA_H
#define WEECHAT_KOLMAFIA_H

#include "weechat-plugin.h"
#include "json/json.h"
#include <random>
#include <map>

namespace WeechatKolmafia { class Plugin; }

extern WeechatKolmafia::Plugin *PluginSingleton;
extern struct t_weechat_plugin *weechat_plugin;

extern "C"
{
  extern int weechat_plugin_init(struct t_weechat_plugin *plugin, int argc, char *argv[]);
  extern int weechat_plugin_end(struct t_weechat_plugin *plugin);
}

namespace WeechatKolmafia
{
  class Plugin
  {
    public:
      Plugin();
      ~Plugin();

      // callbacks
      static int InputWhisperCallback(const void *ptr, void *data, struct t_gui_buffer *weebuf,
          const char *inputData);
      static int InputCliCallback(const void *ptr, void *data, struct t_gui_buffer *weebuf,
          const char *inputData);
      static int CloseWhisperCallback(const void *ptr, void *data, struct t_gui_buffer *weebuf);
      static int CloseCliCallback(const void *ptr, void *data, struct t_gui_buffer *weebuf);
      static int PollCallback(const void *ptr, void *data, int remainingCalls);
      static int UpdateNicklistsCallback(const void *ptr, void *data, int remainingCalls);
      static int PrintHtmlCallback(const void *ptr, void *data, const char *command, int returnCode,
          const char *out, const char *err);

      // commands
#define COMMAND_DECLARATION(CMD) static int CMD##_command_aux(const void *ptr, void *data, \
    struct t_gui_buffer *weebuf, int argc, char **argv, char **argv_eol); \
    int CMD##_command(struct t_gui_buffer *weebuf, int argc, char **argv, char **arv_eol);
      COMMAND_DECLARATION(StartMafia)
      COMMAND_DECLARATION(ReceiveMafia)

      //COMMAND_DECLARATION(me)

    private:
      class Channel;
      struct Config;

      std::string lastSeen;
      struct t_gui_buffer *dbg;
      struct t_gui_buffer *events;
      struct t_gui_buffer *cli;
      Config *conf;
      std::map<std::string, std::string> nameDeuniquifies;
      std::map<std::string, Channel*> channels;
      std::map<std::string, struct t_gui_buffer *> whispers;

      int HttpRequest(const std::string &url, std::string &outbuf);
      void HandleMessage(const Json::Value &msg);
      void PrintHtml(struct t_gui_buffer *buffer, const std::string &html, time_t when = 0,
          const char *tags = nullptr, const char *prefix = nullptr);

      void UpdateSession();

      std::string UrlEncode(const std::string &text);
      std::string NameUniquify(const std::string &name);
      std::string NameDeuniquify(const std::string &name);
      std::string HtmlToWeechat(const std::string &html);
      int SubmitMessage(const std::string &message, std::string &outbuf);
      int SubmitMessage(const std::string &message, struct t_gui_buffer *buffer);
      int HandleInputWhisper(struct t_gui_buffer *weebuf, const char *input_data);
      int HandleInputCli(struct t_gui_buffer *weebuf, const char *input_data);
      int HandleCloseWhisper(struct t_gui_buffer *weebuf);
      int HandleCloseCli(struct t_gui_buffer *weebuf);
      void HandleMafiaEscape(const std::string &key, const std::string &value);
      int PollMessages();

      int SetPollDelay(long newDelay);

      int UpdateAllNicklists();
      Channel *GetChannel(const std::string &channel);
      struct t_gui_buffer *GetWhisperBuffer(const std::string &name);
      struct t_hook *pollHook;
      struct t_hook *updateNicklistsHook;
      long delay;

      std::default_random_engine generator;
      std::uniform_real_distribution<double> distribution;

      bool beGood; // safety flag, set to false in the destructor, NEVER TOUCH OTHERWISE
    };
}

#endif // WEECHAT_KOLMAFIA_H

