// Configuration settings for weechat-kolmafia

#ifndef WEECHAT_KOLMAFIA_CONFIG_H
#define WEECHAT_KOLMAFIA_CONFIG_H

#define KOL_CONFIG_NAME "kol"

#include "weechat-kolmafia.h"

namespace WeechatKolmafia
{
  class Plugin::Config
  {
    public:
      Config(struct t_weechat_plugin *weechat_plugin);
      ~Config();
      int reload();
      int write();

      struct t_config_file *file;

      struct // mafia section
      {
        struct t_config_section *section;

        struct t_config_option *location;
      } mafia;

      struct // session section
      {
        struct t_config_section *section;

        struct t_config_option *hash;
        struct t_config_option *playerid;
        struct t_config_option *playername;
        struct t_config_option *lastloaded;
      } session;

      struct // look section
      {
        struct t_config_section *section;

        struct t_config_option *cli_message_blacklist;
        struct t_config_option *hide_join_part;
      } look;

    private:
      struct t_weechat_plugin *weechat_plugin;
  };
}

#endif // WEECHAT_KOLMAFIA_CONFIG_H

