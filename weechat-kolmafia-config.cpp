#include "weechat-plugin.h"
#include "weechat-kolmafia.h"
#include "weechat-kolmafia-config.h"
#include <limits.h>

//#define KOLMAFIA_LOCATION_DEFAULT "~/.kolmafia"
#define KOLMAFIA_LOCATION_DEFAULT "/mnt/BIGNSLOW/common/Games/kolmafia"

namespace weechat_kolmafia
{
  config::config(struct t_weechat_plugin *plug)
    : weechat_plugin(plug)
  {
    file = weechat_config_new(KOL_CONFIG_NAME, nullptr, nullptr, nullptr);
    if(!file)
      exit(1);

    // mafia
    mafia.section = weechat_config_new_section(file, "mafia",
        0, 0,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr);
    if(!mafia.section)
    {
      weechat_config_free(file);
      exit(1);
    }

    mafia.location = weechat_config_new_option(
        file, mafia.section,
        "location", "string",
        N_("install location of kolmafia (directory that contains the data folder)"),
        nullptr, 0, 0, KOLMAFIA_LOCATION_DEFAULT, nullptr, 0,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    // session
    session.section = weechat_config_new_section(file, "session",
        0, 0,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr);
    if(!session.section)
    {
      weechat_config_free(file);
      exit(1);
    }

    session.hash = weechat_config_new_option(
        file, session.section,
        "hash", "string",
        N_("password hash for this kol session, can be automatically detected via the accompanying ash script"),
        nullptr, 0, 0, "UNSET", nullptr, 0,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    session.playerid = weechat_config_new_option(
        file, session.section,
        "playerid", "integer",
        N_("your player id, can be automatically detected via the accompanying ash script"),
        nullptr, 0, INT_MAX, "0", nullptr, 0,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    session.playername = weechat_config_new_option(
        file, session.section,
        "playername", "string",
        N_("your player name, can be automatically detected via the accompanying ash script"),
        nullptr, 0, 0, "UNSET", nullptr, 0,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    session.lastloaded = weechat_config_new_option(
        file, session.section,
        "lastloaded", "integer",
        N_("the last time that the other session data was loaded, for use with the accompanying ash script"),
        nullptr, 0, INT_MAX, "0", nullptr, 0,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    // cli
    cli.section = weechat_config_new_section(file, "cli",
        0, 0,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr);
    if(!cli.section)
    {
      weechat_config_free(file);
      exit(1);
    }

    cli.message_blacklist = weechat_config_new_option(
        file, cli.section,
        "message_blacklist", "string",
        N_("~ delimited list of phrases which will make a message not show up in the cli"),
        nullptr, 0, 0, "<br><b>Players in channel", nullptr, 0,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
  }

  config::~config()
  {
  }
  int config::reload()
  {
    return 1;
  }
  int config::write()
  {
    return 1;
  }
}

