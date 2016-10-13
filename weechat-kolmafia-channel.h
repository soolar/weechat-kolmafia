#ifndef WEECHAT_KOLMAFIA_CHANNEL_H
#define WEECHAT_KOLMAFIA_CHANNEL_H

#include "weechat-kolmafia.h"

namespace WeechatKolmafia
{
  class Plugin::Channel
  {
    public:
      Channel(const std::string &name);
      ~Channel();

      static int InputCallback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *inputData);
      static int CloseCallback(const void *ptr, void *data, struct t_gui_buffer *weebuf);

      void UpdateNicklist();

      void WriteMessage(time_t when, const std::string &sender, const std::string &message, const std::string &tags);

    private:
      struct t_gui_buffer *buffer;
      const std::string name;

      time_t nicklistLastUpdated;

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

      int HandleInput(const char *inputData);
      int HandleClose();
      int HandlePresenceChange(const Loather &loather, bool isJoining);
  };
}

#endif // WEECHAT_KOLMAFIA_CHANNEL_H

