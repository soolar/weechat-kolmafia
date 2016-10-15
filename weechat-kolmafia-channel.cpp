#include "weechat-kolmafia-channel.h"
#include "weechat-kolmafia-config.h"
#include <htmlcxx/html/ParserDom.h>
#include <string>

namespace WeechatKolmafia
{
  Plugin::Channel::Channel(const std::string &name)
    : name(name), nicklistLastUpdated(0)
  {
    buffer = weechat_buffer_new(name.c_str(), InputCallback, this, nullptr, CloseCallback, this, nullptr);
    weechat_buffer_set(buffer, "nicklist", "1");

    UpdateNicklist();
  }

  Plugin::Channel::~Channel()
  {
    HandleClose();
  }

  int Plugin::Channel::InputCallback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *inputData)
  {
    auto chan = (Plugin::Channel *) ptr;
    (void) data;
    (void) weebuf;
    return chan->HandleInput(inputData);
  }

  int Plugin::Channel::CloseCallback(const void *ptr, void *data, struct t_gui_buffer *weebuf)
  {
    auto chan = (Plugin::Channel *) ptr;
    (void) data;
    (void) weebuf;
    delete chan;
    return WEECHAT_RC_OK;
  }

  void Plugin::Channel::UpdateNicklist()
  {
    time_t now = time(nullptr);
    if(now - nicklistLastUpdated < 60 || (weechat_buffer_get_integer(buffer, "num_displayed") < 1 && nicklistLastUpdated != 0)) // only update if it's been a minute since last update and window is active (or hasn't been updated before
      return;
    weechat_printf(PluginSingleton->dbg, "Updating %s nicklist", name.c_str());
    std::string message("/who ");
    message += weechat_buffer_get_string(buffer, "name");
    std::string res;
    if(PluginSingleton->SubmitMessage(message, res) == WEECHAT_RC_OK)
    {
      std::map<std::string, Loather> loathersNow;

      weechat_nicklist_remove_all(buffer);
      // TODO: change these colors to be configurable
      friends = weechat_nicklist_add_group(buffer, nullptr, "1|friends", "blue,0", 0);
      clannies = weechat_nicklist_add_group(buffer, nullptr, "2|clannies", "green,0", 0);
      others = weechat_nicklist_add_group(buffer, nullptr, "3|others", "bar_fg,0", 0);
      away = weechat_nicklist_add_group(buffer, nullptr, "4|away", "11,0", 0);

      Json::Value v;
      Json::Reader r;
      r.parse(res, v);
      std::string output = v["output"].asString();

      htmlcxx::HTML::ParserDom parser;
      tree<htmlcxx::HTML::Node> dom = parser.parseTree(output);

      bool nextIsFriend = false;
      bool nextIsAway = false;
      bool nextIsName = false;

      for(auto it = dom.begin(); it != dom.end(); ++it)
      {
        /*
        std::ostringstream debugprint;
        debugprint << weechat_color("red") << "tag[" << weechat_color("resetcolor") <<
          it->tagName() << weechat_color("red") << "] " << weechat_color("green") <<
          "text[" << weechat_color("resetcolor") << it->text() << weechat_color("green") <<
          "] " << weechat_color("blue") << "closing[" << weechat_color("resetcolor") <<
          it->closingText() << weechat_color("blue") << "]";
        std::string debugprintfinal(debugprint.str());
        weechat_printf(dbg, "%s", debugprintfinal.c_str());
        //*/
        if(it->isTag())
        {
          if(it->tagName() == "font")
          {
            it->parseAttributes();
            auto color = it->attribute("color");
            if(color.first)
            {
              if(color.second == "blue")
                nextIsFriend = true;
            }
            nextIsName = true;
          }
          else if(it->tagName() == "a")
          {
            it->parseAttributes();
            auto cls = it->attribute("class");
            if(cls.first && cls.second == "afk")
              nextIsAway = true;
          }
        }
        else if(nextIsName)
        {
          // TODO: clannies detection
          struct t_gui_nick_group *group = others;
          if(nextIsFriend) group = friends;
          if(nextIsAway) group = away;

          loathersNow.emplace(PluginSingleton->NameUniquify(it->text()),
              Loather(it->text(), nextIsFriend, false, nextIsAway));
          nextIsName = nextIsAway = nextIsFriend = false;

          weechat_nicklist_add_nick(buffer, group, it->text().c_str(), nullptr, nullptr, nullptr, 1);
          weechat_nicklist_group_set(buffer, group, "visible", "1");
        }
      }

      // first, handle all the leavers
      for(auto it = loathers.begin(); it != loathers.end(); ++it)
      {
        if(loathersNow.find(it->first) == loathersNow.end())
        {
          HandlePresenceChange(it->second, false);
        }
      }
      // now handle joins
      for(auto it = loathersNow.begin(); it != loathersNow.end(); ++it)
      {
        auto old = loathers.find(it->first);
        if(old == loathers.end())
        {
          HandlePresenceChange(it->second, true);
        }
        // TODO: detect changing to/from away status
      }

      loathers = loathersNow;
      nicklistLastUpdated = now;
    }
  }

  void Plugin::Channel::WriteMessage(time_t when, const std::string &sender, const std::string &message, const std::string &tags)
  {
    PluginSingleton->PrintHtml(buffer, message, when, tags.c_str(), sender.c_str());
  }

  int Plugin::Channel::HandleInput(const char *inputData)
  {
    std::string message("/");
    if(inputData[0] != ';')
    {
      message += weechat_buffer_get_string(buffer, "name");
      message += " ";
    }
    else
    {
      inputData += 1;
    }
    message += inputData;

    std::string res;
    if(PluginSingleton->SubmitMessage(message, res) == WEECHAT_RC_ERROR)
      return WEECHAT_RC_ERROR;

    //weechat_printf(dbg,"%s",buffer.c_str());
    Json::Value v;
    Json::Reader r;
    r.parse(res, v);
    std::string output = v["output"].asString();
    if(!output.empty())
      PluginSingleton->PrintHtml(buffer, output);
    return WEECHAT_RC_OK;
  }

  int Plugin::Channel::HandleClose()
  {
    weechat_nicklist_remove_all(buffer);
    PluginSingleton->channels.erase(name);
    return WEECHAT_RC_OK;
  }

  int Plugin::Channel::HandlePresenceChange(const Loather &loather, bool isJoining)
  {
    auto conf = PluginSingleton->conf;
    std::string blacklist(weechat_config_string(conf->look.hide_join_part));
    if(blacklist.find(loather.name) != std::string::npos)
      return WEECHAT_RC_OK;

    auto colorconf = weechat_config_get(isJoining ? "weechat.color.chat_prefix_join" :
                                                    "weechat.color.chat_prefix_quit");
    auto color = weechat_config_color(colorconf);
    const char *colorstr = weechat_color(color);
    auto prefixconf = weechat_config_get(isJoining ? "weechat.look.prefix_join" :
                                                     "weechat.look.prefix_quit");
    const char *prefix = weechat_config_string(prefixconf);
    const char *reset = weechat_color("resetcolor");
    const char *playername = loather.name.c_str();
    weechat_printf_date_tags(buffer, 0, isJoining? "kol_join_message" : "kol_leave_message",
        "%s%s%s\t%s has %s %s", colorstr, prefix, reset, playername, 
        isJoining ? "joined" : "left", name.c_str());

    return WEECHAT_RC_OK;
  }
} // namespace WeechatKolmafia

