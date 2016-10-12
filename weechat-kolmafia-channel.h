#ifndef WEECHAT_KOLMAFIA_CHANNEL_H
#define WEECHAT_KOLMAFIA_CHANNEL_H

#include "weechat-kolmafia.h"

namespace weechat_kolmafia
{
  class plugin::channel
  {
    public:
      channel(plugin *plug, const std::string &name);
      ~channel();

      static int input_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *input_data);
      static int close_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf);

      void update_nicklist();

      void write_message(time_t when, const std::string &sender, const std::string &message, const std::string &tags);

    private:
      struct t_weechat_plugin *weechat_plugin;
      struct t_gui_buffer *buffer;
      const std::string name;

      plugin *plug;

      const config *conf;

      time_t nicklist_last_updated;

      struct Loather
      {
        Loather(const std::string &name, bool isFriend, bool isClanmate, bool isAway)
          : name(name), isFriend(isFriend), isClanmate(isClanmate), isAway(isAway)
        {
          // construction handled entirely by initializer list
        }

        std::string name;
        bool isFriend, isClanmate, isAway;
        struct t_gui_nick_group *currGroup;
      };
      std::map<std::string, Loather> loathers;

      struct t_gui_nick_group *friends;
      struct t_gui_nick_group *clannies;
      struct t_gui_nick_group *others;
      struct t_gui_nick_group *away;

      int handle_input(const char *input_data);
      int handle_close();
  };
}

#endif // WEECHAT_KOLMAFIA_CHANNEL_H

