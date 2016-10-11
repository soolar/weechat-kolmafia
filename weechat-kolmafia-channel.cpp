#include "weechat-kolmafia-channel.h"
#include <htmlcxx/html/ParserDom.h>
#include <string>

namespace weechat_kolmafia
{
  plugin::channel::channel(plugin *plug, const std::string &name)
    : weechat_plugin(plug->weechat_plugin), nicklist_last_updated(0), plug(plug)
  {
    buffer = weechat_buffer_new(name.c_str(), input_callback, this, nullptr, close_callback, this, nullptr);
    weechat_buffer_set(buffer, "nicklist", "1");
    
    // TODO: change these colors to be configurable
    friends = weechat_nicklist_add_group(buffer, nullptr, "1|friends", "blue,0", 1);
    clannies = weechat_nicklist_add_group(buffer, nullptr, "2|clannies", "green,0", 1);
    others = weechat_nicklist_add_group(buffer, nullptr, "3|others", "bar_fg,0", 1);
    away = weechat_nicklist_add_group(buffer, nullptr, "4|away", "11,0", 1);

    update_nicklist();
  }

  plugin::channel::~channel()
  {
    
  }

  int plugin::channel::input_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *input_data)
  {
    auto chan = (plugin::channel *) ptr;
    (void) data;
    return chan->handle_input(input_data);
  }

  int plugin::channel::close_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf)
  {
    auto chan = (plugin::channel *) ptr;
    (void) data;
    int code = chan->handle_close();
    delete chan;
    return code;
  }

  void plugin::channel::update_nicklist()
  {
    std::string message("/who ");
    message += weechat_buffer_get_string(buffer, "name");
    std::string res;
    if(plug->submit_message(message, res) == WEECHAT_RC_OK)
    {
      weechat_nicklist_remove_all(buffer);
      weechat_nicklist_add_group(buffer, nullptr, "1|friends", "blue,0", 1);
      weechat_nicklist_add_group(buffer, nullptr, "2|clannies", "green,0", 1);
      weechat_nicklist_add_group(buffer, nullptr, "3|others", "bar_fg,0", 1);
      weechat_nicklist_add_group(buffer, nullptr, "4|away", "11,0", 1);

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
          const char *group_name = "others";
          if(nextIsFriend) group_name = "friends";
          if(nextIsAway) group_name = "away";
          struct t_gui_nick_group *group = weechat_nicklist_search_group(buffer, nullptr, group_name);
          if(group != nullptr)
          {
            auto nick = weechat_nicklist_add_nick(buffer, group, it->text().c_str(), "bar_fg", "", "", 1);
            if(nick == nullptr) weechat_printf(buffer, "Problem adding nick %s to group %s", it->text().c_str(), group_name);
          }
          else weechat_printf(buffer, "Something got messed bruh [%s][%s]",group_name,it->text().c_str());

          nextIsName = nextIsAway = nextIsFriend = false;
        }
      }
    }
  }

  void plugin::channel::write_message(time_t when, const std::string &sender, const std::string &message, const std::string &tags)
  {
    weechat_printf_date_tags(buffer, when, tags.c_str(), "%s\t%s", sender.c_str(), message.c_str());
  }

  int plugin::channel::handle_input(const char *input_data)
  {
    std::string message("/");
    if(input_data[0] != ';')
    {
      message += weechat_buffer_get_string(buffer, "name");
      message += " ";
    }
    else
    {
      input_data += 1;
    }
    message += input_data;

    std::string res;
    if(plug->submit_message(message, res) == WEECHAT_RC_ERROR)
      return WEECHAT_RC_ERROR;

    //weechat_printf(dbg,"%s",buffer.c_str());
    Json::Value v;
    Json::Reader r;
    r.parse(res, v);
    std::string output = v["output"].asString();
    if(!output.empty())
      weechat_printf(buffer, "%s", plug->html_to_weechat(output).c_str());
    return WEECHAT_RC_OK;
  }

  int plugin::channel::handle_close()
  {
    plug->channels.erase(weechat_buffer_get_string(buffer, "name"));
    return WEECHAT_RC_OK;
  }
} // namespace weechat_kolmafia

