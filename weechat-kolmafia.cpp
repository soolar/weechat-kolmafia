#include "weechat-kolmafia.h"
#include "weechat-kolmafia-config.h"
#include <iostream>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <curl/curl.h>
#include "json/json.h"
#include <htmlcxx/html/ParserDom.h>
#include <htmlcxx/html/utils.h>
#include <sstream>

WEECHAT_PLUGIN_NAME("kol")
WEECHAT_PLUGIN_AUTHOR("soolar")
WEECHAT_PLUGIN_DESCRIPTION("uses kolmafia's session to interface with Kingdom of Loathing's in game chat")
WEECHAT_PLUGIN_VERSION("0.1")
WEECHAT_PLUGIN_LICENSE("GPL3")

#define URL(PAGE) "http://127.0.0.1:60080/" PAGE

namespace
{
  weechat_kolmafia::plugin *plugin_singleton = nullptr;

  int writer(char *data, size_t size, size_t nmemb, std::string *writerData)
  {
    if(writerData == nullptr)
      return 0;

    writerData->append(data, size*nmemb);

    return size * nmemb;
  }

  char to_hex(char c)
  {
    if(c < 10)
      return c + '0';
    return c - 10 + 'A';
  }
} // anonymous namespace

int weechat_plugin_init(struct t_weechat_plugin *plugin, int argc, char *argv[])
{
  plugin_singleton = new weechat_kolmafia::plugin(plugin);
  return WEECHAT_RC_OK;
}

int weechat_plugin_end(struct t_weechat_plugin *plugin)
{
  delete plugin_singleton;
  return WEECHAT_RC_OK;
}

namespace weechat_kolmafia
{
  // public
  plugin::plugin(struct t_weechat_plugin *plug)
    : weechat_plugin(plug), conf(plug), distribution(0.0, 1.0), poll_hook(nullptr), delay(0), be_good(true)
  {
    update_session();

    curl_global_init(CURL_GLOBAL_ALL);
    lastSeen = "0";

    dbg = weechat_buffer_new("debug", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    events = weechat_buffer_new("events", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    cli = weechat_buffer_new("mafia", input_cli_callback, this, nullptr, close_cli_callback, this, nullptr);
    weechat_buffer_set(dbg, "notify", "0");
    get_channel_buffer("talkie");
    get_channel_buffer("clan");

    set_poll_delay(3000);
    poll_cli_hook = weechat_hook_timer(1000, 1, 0, poll_cli_callback, this, nullptr);
    update_nicklists_hook = weechat_hook_timer(60000, 60, 0, update_nicklists_callback, this, nullptr);

#define HOOK_COMMAND(CMD, DESC, ARGS, ARGS_DESC, COMPLETION) weechat_hook_command(#CMD, DESC, ARGS, ARGS_DESC, COMPLETION, plugin::CMD##_command_aux, this, nullptr);
    HOOK_COMMAND(who, "Gets a list of players in the current channel", "", "", "")
  }

  plugin::~plugin()
  {
    be_good = false;

    weechat_unhook(poll_hook);
    weechat_unhook(poll_cli_hook);
    weechat_unhook(update_nicklists_hook);
    curl_global_cleanup();
  }

  // callbacks
  int plugin::input_channel_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *input_data)
  {
    plugin *plug = (plugin *) ptr;
    (void) data;
    return plug->handle_input_channel(weebuf, input_data);
  }

  int plugin::input_whisper_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *input_data)
  {
    plugin *plug = (plugin *) ptr;
    (void) data;
    return plug->handle_input_whisper(weebuf, input_data);
  }

  int plugin::input_cli_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *input_data)
  {
    plugin *plug = (plugin *) ptr;
    (void) data;
    return plug->handle_input_cli(weebuf, input_data);
  }

  int plugin::close_channel_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf)
  {
    plugin *plug = (plugin *) ptr;
    (void) data;
    return plug->handle_close_channel(weebuf);
  }

  int plugin::close_whisper_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf)
  {
    plugin *plug = (plugin *) ptr;
    (void) data;
    return plug->handle_close_whisper(weebuf);
  }

  int plugin::close_cli_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf)
  {
    plugin *plug = (plugin *) ptr;
    (void) data;
    return plug->handle_close_cli(weebuf);
  }
  
  int plugin::poll_callback(const void *ptr, void *data, int remaining_calls)
  {
    plugin *plug = (plugin *) ptr;
    (void) data;
    (void) remaining_calls;
    return plug->poll_messages();
  }

  int plugin::poll_cli_callback(const void *ptr, void *data, int remaining_calls)
  {
    plugin *plug = (plugin *) ptr;
    (void) data;
    (void) remaining_calls;
    return plug->poll_cli_messages();
  }
  
  int plugin::update_nicklists_callback(const void *ptr, void *data, int remaining_calls)
  {
    plugin *plug = (plugin *) ptr;
    (void) data;
    (void) remaining_calls;
    return plug->update_all_nicklists();
  }

  // commands
#define COMMAND_FUNCTION(CMD) int plugin::CMD##_command_aux(const void *ptr, void *data, struct t_gui_buffer *weebuf, int argc, char **argv, char **argv_eol) { plugin *plug = (plugin *) ptr; return plug->CMD##_command(weebuf, argc, argv, argv_eol); } int plugin::CMD##_command(struct t_gui_buffer *weebuf, int argc, char **argv, char **argv_eol)
  COMMAND_FUNCTION(me)
  {
    return WEECHAT_RC_OK;
  }

  COMMAND_FUNCTION(who)
  {
    update_nicklist(weebuf);

    return WEECHAT_RC_OK;
  }

  // private
  int plugin::http_request(const std::string &url, std::string &outbuf)
  {
    //weechat_printf(dbg, "GET %s", url.c_str());
    CURL *curl;
    CURLcode res;
    char errorBuffer[CURL_ERROR_SIZE];

    curl = curl_easy_init();
    if(!curl)
    {
      weechat_printf(dbg, "Failed to initialize curl.");
      return WEECHAT_RC_ERROR;
    }
    res = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    if(res != CURLE_OK)
    {
      weechat_printf(dbg, "Failed to set error buffer [%d]", res);
      return WEECHAT_RC_ERROR;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if(res != CURLE_OK)
    {
      weechat_printf(dbg, "Failed to set url [%s]", errorBuffer);
      return WEECHAT_RC_ERROR;
    }
    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writer);
    if(res != CURLE_OK)
    {
      weechat_printf(dbg, "Failed to set writer [%s]", errorBuffer);
      return WEECHAT_RC_ERROR;
    }
    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outbuf);
    if(res != CURLE_OK)
    {
      weechat_printf(dbg, "Failed to set write data [%s]", errorBuffer);
      return WEECHAT_RC_ERROR;
    }
    res = curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 500);
    if(res != CURLE_OK)
    {
      weechat_printf(dbg, "Failed to set timeout [%s]", errorBuffer);
      return WEECHAT_RC_ERROR;
    }
    struct curl_slist *list = nullptr;
    list = curl_slist_append(list, "Connection: keep-alive");
    list = curl_slist_append(list, "Accept: application/json, text/javascript, */*; q=0.01");
    list = curl_slist_append(list, "X-Requested-With: XMLHttpRequest");
    list = curl_slist_append(list, "Accept-Language: en-US,en;q=0.8");
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    if(res != CURLE_OK)
    {
      weechat_printf(dbg, "Failed to set http header [%s]", errorBuffer);
      return WEECHAT_RC_ERROR;
    }
    res = curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/53.0.2785.116 Safari/537.36");
    if(res != CURLE_OK)
    {
      weechat_printf(dbg, "Failed to set user agent [%s]", errorBuffer);
      return WEECHAT_RC_ERROR;
    }
    res = curl_easy_setopt(curl, CURLOPT_REFERER, "http://127.0.0.1:60080/mchat.php");
    if(res != CURLE_OK)
    {
      weechat_printf(dbg, "Failed to set referer [%s]", errorBuffer);
      return WEECHAT_RC_ERROR;
    }
    res = curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate, sdch");
    if(res != CURLE_OK)
    {
      weechat_printf(dbg, "Failed to set accept-encoding [%s]", errorBuffer);
      return WEECHAT_RC_ERROR;
    }

    res = curl_easy_perform(curl);
    if(res != CURLE_OK)
    {
      weechat_printf(dbg, "curl_easy_perform(%s) failed: %s", url.c_str(), errorBuffer);
      return WEECHAT_RC_ERROR;
    }
    curl_easy_cleanup(curl);

    return WEECHAT_RC_OK;
  }

  std::string plugin::html_to_weechat(const std::string& html)
  {
    std::string parsed;

    htmlcxx::HTML::ParserDom parser;
    tree<htmlcxx::HTML::Node> dom = parser.parseTree(html);

    //weechat_printf(dbg, "%sParsing:%s [%s]", weechat_color("red"), weechat_color("resetcolor"), html.c_str());
    for(auto it = dom.begin(); it != dom.end(); ++it)
    {
      //weechat_printf(dbg, "%sOffset:%s [%u], Tag name: [%s], Text: [%s], Closing Text: [%s]", weechat_color("green"), weechat_color("resetcolor"), it->offset(), it->tagName().c_str(), it->text().c_str(), it->closingText().c_str());
      if(it->isTag())
      {
        if(it->tagName() == "font")
        {
          it->parseAttributes();
          auto attr = it->attribute("color");
          if(attr.first)
          {
            std::string color("|"); // keep other attributes
            color += attr.second;
            parsed += weechat_color(color.c_str());
          }
          else
          {
            parsed += weechat_color("resetcolor");
          }
        }
        else if(it->tagName() == "br")
        {
          // don't do multiple newlines in a row or at the start
          if(!parsed.empty() && parsed.back() != '\n')
            parsed += '\n';
        }
      }
      else if(!it->isComment())
      {
        std::string decoded = htmlcxx::HTML::decode_entities(it->text());
        for(auto dit = decoded.begin(); dit != decoded.end(); ++dit)
        {
          if(*dit == '\n')
          {
            if(!parsed.empty() && parsed.back() != '\n')
              parsed += *dit;
          }
          else
          {
            parsed += *dit;
          }
        }
      }
    }

    // get rid of any potential ending newlines
    if(!parsed.empty())
    {
      while(parsed.back() == '\n')
        parsed.erase(parsed.size() - 1);
    }

    return parsed;
  }

  void plugin::handle_message(const Json::Value &msg)
  {
    std::string sender = msg["who"]["name"].asString();
    std::string body = msg["msg"].asString();
    time_t when = std::stoul(msg["time"].asString());
    std::string tags("nick_");
    tags += name_uniquify(sender);
    std::string type = msg["type"].asString();

    std::string parsed(html_to_weechat(body));

    if(type == "event")
    {
      weechat_printf_date_tags(events, when, "kol_event", "\t%s", parsed.c_str());
    }
    else if(type == "system")
    {
      weechat_printf_date_tags(events, when, "kol_system", "\t%s%s", "System Message: ", parsed.c_str());
    }
    else if(type == "public")
    {
      std::string channel = msg["channel"].asString();

      int format = std::stoi(msg["format"].asString());
      switch(format)
      {
        case 0: // normal message
          break;
        case 1: // emote
          sender = weechat_color(weechat_config_string(weechat_config_get("weechat.color.chat_prefix_action")));
          sender += weechat_config_string(weechat_config_get("weechat.look.prefix_action"));
          sender += weechat_color("resetcolor");
          break;
        case 2: // system message
          // public system messages are no longer used, or so I have been told
          weechat_printf(dbg, "%sWHOEVER TOLD YOU PUBLIC SYSTEM MESSAGES WERE NO LONGER USED LIED", weechat_color("red"));
          break;
        case 3: // mod warning
          // TODO: Decide how to format this after actually seeing one??
          break;
        case 4: // mod announcement
          // TODO: Decide how to format this after actually seeing one??
          break;
        default:
          weechat_printf(dbg, "Unrecognized message format %d", format);
          break;
      }

      struct t_gui_buffer *buf = get_channel_buffer(channel.c_str());
      weechat_printf_date_tags(buf, when, tags.c_str(), "%s\t%s", sender.c_str(), parsed.c_str());
    }
    else if(type == "private")
    {
      struct t_gui_buffer *buf = get_whisper_buffer(sender.c_str());
      weechat_printf_date_tags(buf, when, tags.c_str(), "%s\t%s", sender.c_str(), parsed.c_str());
    }
    else
    {
      weechat_printf(dbg, "Unrecognized message type \"%s\"", type.c_str());
    }
  }

  void plugin::update_session()
  {
    std::string fileName(weechat_config_string(conf.mafia.location));
    fileName += "/data/weechat.txt";
    struct stat t_stat;
    stat(fileName.c_str(), &t_stat);
    time_t editted = t_stat.st_ctime;
    time_t known = weechat_config_integer(conf.session.lastloaded);

    if(known < editted)
    {
      // session data is outdated, time to update
      //weechat_printf(dbg, "Current session data from %d, actual session data from %d, reloading session.", known, editted);
      std::filebuf fb;
      if(fb.open(fileName, std::ios::in))
      {
        std::istream is(&fb);
        std::string line;
        std::getline(is, line);
        weechat_config_option_set(conf.session.hash, line.c_str() + 2, 0);
        std::getline(is, line);
        weechat_config_option_set(conf.session.playerid, line.c_str() + 2, 0);
        std::getline(is, line);
        weechat_config_option_set(conf.session.playername, line.c_str() + 2, 0);
        char timestamp[1024]; // a 64 bit integer can be 19 digits at most, so 1024 should definitely be enough room
        sprintf(timestamp, "%ld", editted);
        weechat_config_option_set(conf.session.lastloaded, timestamp, 0);
      }
    }
  }

  std::string plugin::url_encode(const std::string &text)
  {
    static const std::string safe_chars(
        "0123456789-_.!~*'()"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz");
    const char * hex = "0123456789ABCDEF";

    std::string encoded;
    for(auto it = text.begin(); it != text.end(); ++it)
    {
      char c = *it;
      if(c == ' ')
        encoded += '+';
      else if(safe_chars.find(c) != std::string::npos)
        encoded += c;
      else
      {
        encoded += '%';
        encoded += hex[(c >> 4) & 0xF];
        encoded += hex[c & 0xF];
      }
    }
    return encoded;
  }

  std::string plugin::name_uniquify(const std::string &name)
  {
    std::string encoded;
    for(auto it = name.begin(); it != name.end(); ++it)
    {
      if(*it == ' ')
        encoded += '_';
      else if(*it >= 'A' && *it <= 'Z')
        encoded += (*it + 'a' - 'A');
      else
        encoded += *it;
    }
    name_deuniquifies[encoded] = name;
    return encoded;
  }

  std::string plugin::name_deuniquify(const std::string &name)
  {
    return name_deuniquifies[name];
  }

  int plugin::submit_message(const std::string &message, std::string &outbuf)
  {
    update_session();

    std::string url(URL("submitnewchat.php?playerid="));
    url += std::to_string(weechat_config_integer(conf.session.playerid));
    url += "&pwd=";
    url += weechat_config_string(conf.session.hash);
    url += "&graf=";
    url += url_encode(message);
    url += "&j=1";

    if(http_request(url, outbuf) != WEECHAT_RC_OK)
      return WEECHAT_RC_ERROR;

    return WEECHAT_RC_OK;
  }

  int plugin::handle_input_channel(struct t_gui_buffer *weebuf, const char *input_data)
  {
    std::string message("/");
    if(input_data[0] != ';')
    {
      message += weechat_buffer_get_string(weebuf, "name");
      message += " ";
    }
    else
    {
      input_data += 1;
    }
    message += input_data;

    std::string buffer;
    if(submit_message(message, buffer) == WEECHAT_RC_ERROR)
      return WEECHAT_RC_ERROR;

    //weechat_printf(dbg,"%s",buffer.c_str());
    Json::Value v;
    Json::Reader r;
    r.parse(buffer, v);
    std::string output = v["output"].asString();
    if(!output.empty())
      weechat_printf(weebuf, "%s", html_to_weechat(output).c_str());
    return WEECHAT_RC_OK;
  }

  int plugin::handle_input_whisper(struct t_gui_buffer *weebuf, const char *input_data)
  {
    std::string message("/msg ");
    message += weechat_buffer_get_string(weebuf, "name");
    message += " ";
    message += input_data;

    weechat_printf(weebuf, "%s\t%s", weechat_config_string(conf.session.playername), input_data);

    std::string buffer;
    return submit_message(message, buffer);
  }

  int plugin::handle_input_cli(struct t_gui_buffer *weebuf, const char *input_data)
  {
    std::string url(URL("/KoLmafia/submitCommand?cmd="));
    url += url_encode(input_data);
    url += "&pwd=";
    url += weechat_config_string(conf.session.hash);
    std::string buffer;
    return http_request(url, buffer);
  }

  int plugin::handle_close_channel(struct t_gui_buffer *weebuf)
  {
    channels.erase(weechat_buffer_get_string(weebuf, "name"));
    return WEECHAT_RC_OK;
  }

  int plugin::handle_close_whisper(struct t_gui_buffer *weebuf)
  {
    whispers.erase(name_uniquify(weechat_buffer_get_string(weebuf, "name")));
    return WEECHAT_RC_OK;
  }

  int plugin::handle_close_cli(struct t_gui_buffer *weebuf)
  {
    return WEECHAT_RC_OK;
  }

  int plugin::poll_messages()
  {
    update_session();

    std::string url(URL("newchatmessages.php?aa="));
    url += std::to_string(distribution(generator));
    url += "&j=1&lasttime=";
    url += lastSeen;
    std::string buffer;
    if(http_request(url, buffer) != WEECHAT_RC_OK)
      return WEECHAT_RC_ERROR;

    //weechat_printf(dbg, "%s", buffer.c_str());
    Json::Value v;
    Json::Reader r;
    r.parse(buffer, v);
    Json::Value msgs = v["msgs"];

    lastSeen = v["last"].asString();
    set_poll_delay(v["delay"].asInt64());

    for(Json::ArrayIndex i = 0; i < msgs.size(); ++i)
    {
      handle_message(msgs[i]);
    }

    return WEECHAT_RC_OK;
  }

  int plugin::poll_cli_messages()
  {
    update_session();

    std::string url(URL("KoLmafia/messageUpdate?pwd="));
    url += weechat_config_string(conf.session.hash);

    std::string res;
    if(http_request(url, res) == WEECHAT_RC_ERROR)
      return WEECHAT_RC_ERROR;
    if(!res.empty() && res != " ")
    {
      std::istringstream ss(weechat_config_string(conf.cli.message_blacklist));
      std::string blacklisted;
      while(std::getline(ss, blacklisted, '~'))
      {
        if(res.find(blacklisted) != std::string::npos)
          return WEECHAT_RC_OK;
      }

      std::string parsed(html_to_weechat(res));
      weechat_printf(dbg, "%s", res.c_str());
      weechat_printf(cli, "%s", parsed.c_str());
    }

    return WEECHAT_RC_OK;
  }

  int plugin::update_all_nicklists()
  {
    for(auto it = channels.begin(); it != channels.end(); ++it)
    {
      update_nicklist(it->second);
    }
    
    return WEECHAT_RC_OK;
  }

  int plugin::set_poll_delay(long newdelay)
  {
    if(delay == newdelay)
      return WEECHAT_RC_OK;

    if(poll_hook != nullptr)
      weechat_unhook(poll_hook);

    delay = newdelay;
    poll_hook = weechat_hook_timer(newdelay, 1, 0, poll_callback, this, nullptr);

    return WEECHAT_RC_OK;
  }

  void plugin::update_nicklist(struct t_gui_buffer *channel)
  {
    std::string message("/who ");
    message += weechat_buffer_get_string(channel, "name");
    std::string res;
    if(submit_message(message, res) == WEECHAT_RC_OK)
    {
      weechat_nicklist_remove_all(channel);
      weechat_nicklist_add_group(channel, nullptr, "1|friends", "blue,0", 1);
      weechat_nicklist_add_group(channel, nullptr, "2|clannies", "green,0", 1);
      weechat_nicklist_add_group(channel, nullptr, "3|others", "bar_fg,0", 1);
      weechat_nicklist_add_group(channel, nullptr, "4|away", "11,0", 1);

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
          struct t_gui_nick_group *group = weechat_nicklist_search_group(channel, nullptr, group_name);
          if(group != nullptr)
          {
            auto nick = weechat_nicklist_add_nick(channel, group, it->text().c_str(), "bar_fg", "", "", 1);
            if(nick == nullptr) weechat_printf(channel, "Problem adding nick %s to group %s", it->text().c_str(), group_name);
          }
          else weechat_printf(channel, "Something got messed bruh [%s][%s]",group_name,it->text().c_str());

          nextIsName = nextIsAway = nextIsFriend = false;
        }
      }
    }
  }

  struct t_gui_buffer *plugin::get_channel_buffer(const std::string &channel)
  {
    struct t_gui_buffer *buf = weechat_buffer_search("kol", channel.c_str());
    if(buf == nullptr)
    {
      buf = weechat_buffer_new(channel.c_str(), input_channel_callback, this, nullptr, close_channel_callback, this, nullptr);
      weechat_buffer_set(buf, "nicklist", "1");
      channels[channel] = buf;
      update_nicklist(buf);
    }
    return buf;
  }

  struct t_gui_buffer *plugin::get_whisper_buffer(const std::string &name)
  {
    std::string encoded = name_uniquify(name);
    struct t_gui_buffer *buf = weechat_buffer_search("kol", encoded.c_str());
    if(buf == nullptr)
      buf = weechat_buffer_new(encoded.c_str(), input_whisper_callback, this, nullptr, close_whisper_callback, this, nullptr);
    whispers[name] = buf;
    return buf;
  }
} // namespace weechat_kolmafia

