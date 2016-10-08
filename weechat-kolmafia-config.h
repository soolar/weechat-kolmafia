// Configuration settings for weechat-kolmafia

#ifndef WEECHAT_KOLMAFIA_CONFIG_H
#define WEECHAT_KOLMAFIA_CONFIG_H

#define KOL_CONFIG_NAME "kol"

namespace weechat_kolmafia
{
  class config
  {
    public:
      config(struct t_weechat_plugin *weechat_plugin);
      ~config();
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

    private:
      struct t_weechat_plugin *weechat_plugin;
  };
}

#endif // WEECHAT_KOLMAFIA_CONFIG_H

