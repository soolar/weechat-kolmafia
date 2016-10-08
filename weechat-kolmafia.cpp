#include "weechat-kolmafia.h"
#include "weechat-kolmafia-config.h"
#include <iostream>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <curl/curl.h>
#include "json/json.h"

WEECHAT_PLUGIN_NAME("kol")
WEECHAT_PLUGIN_AUTHOR("soolar")
WEECHAT_PLUGIN_DESCRIPTION("uses kolmafia's session to interface with Kingdom of Loathing's in game chat")
WEECHAT_PLUGIN_VERSION("0.1")
WEECHAT_PLUGIN_LICENSE("GPL3")

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
    : weechat_plugin(plug), conf(plug), distribution(0.0, 1.0), poll_hook(nullptr), delay(0)
  {
    update_session();

    curl_global_init(CURL_GLOBAL_ALL);
    
    lastSeen = "0";
    set_poll_delay(3000);

    dbg = weechat_buffer_new("debug", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    get_channel_buffer("talkie");
    get_channel_buffer("clan");
    get_channel_buffer("games");
  }

  plugin::~plugin()
  {
    curl_global_cleanup();
  }

  // callbacks
  int plugin::input_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *input_data)
  {
    plugin *plug = (plugin *) ptr;
    (void) data;
    return plug->handle_input(weebuf, input_data);
  }

  int plugin::close_callback(const void *ptr, void *data, struct t_gui_buffer *weebuf)
  {
    plugin *plug = (plugin *) ptr;
    (void) data;
    return plug->handle_close(weebuf);
  }

  int plugin::poll_callback(const void *ptr, void *data, int remaining_calls)
  {
    plugin *plug = (plugin *) ptr;
    (void) data;
    (void) remaining_calls;
    return plug->poll_messages();
  }
 
  // private
  int plugin::http_request(const std::string &url, std::string &outbuf)
  {
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
    list - curl_slist_append(list, "Connection: keep-alive");
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

  void plugin::handle_message(const Json::Value &msg)
  {
    std::string channel = msg["channel"].asString();
    struct t_gui_buffer *buf = get_channel_buffer(channel.c_str());
    std::string sender = msg["who"]["name"].asString();
    std::string body = msg["msg"].asString();
    time_t when = std::stoul(msg["time"].asString());
    std::string tags("nick_");
    std::string name(msg["name"].asString());
    for(auto it = name.begin(); it != name.end(); ++it)
      tags += (*it == ' ' ? '+' : *it);
    weechat_printf_date_tags(buf, when, tags.c_str(), "%s\t%s", sender.c_str(), body.c_str());
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

  int plugin::submit_message(const std::string &message)
  {
    std::string url(URL("submitnewchat.php?playerid="));
    url += std::to_string(weechat_config_integer(conf.session.playerid));
    url += "&pwd=";
    url += weechat_config_string(conf.session.hash);
    url += "&graf=";
    url += url_encode(message);
    url += "&j=1";

    std::string buffer;
    if(http_request(url, buffer) != WEECHAT_RC_OK)
      return WEECHAT_RC_ERROR;

    return WEECHAT_RC_OK;
  }

  int plugin::handle_input(struct t_gui_buffer *weebuf, const char *input_data)
  {
    std::string message("/");
    message += weechat_buffer_get_string(weebuf, "name");
    message += " ";
    message += input_data;

    return submit_message(message);
  }

  int plugin::handle_close(struct t_gui_buffer *weebuf)
  {
    return WEECHAT_RC_OK;
  }

  int plugin::poll_messages()
  {
    update_session();

    std::string url("http://127.0.0.1:60080/newchatmessages.php?aa=");
    url += std::to_string(distribution(generator));
    url += "&j=1&lasttime=";
    url += lastSeen;
    std::string buffer;
    if(http_request(url, buffer) != WEECHAT_RC_OK)
      return WEECHAT_RC_ERROR;

    Json::Value v;
    Json::Reader r;
    r.parse(buffer, v);
    Json::Value msgs = v["msgs"];

    lastSeen = v["last"].asString();
    set_poll_delay(v["delay"].asInt64());

    weechat_printf(dbg, "%s", buffer.c_str());
    for(Json::ArrayIndex i = 0; i < msgs.size(); ++i)
    {
      handle_message(msgs[i]);
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
  }

  struct t_gui_buffer *plugin::get_channel_buffer(const char *channel)
  {
    struct t_gui_buffer *buf = weechat_buffer_search("kol", channel);
    if(buf == nullptr)
      buf = weechat_buffer_new(channel, input_callback, this, nullptr, close_callback, this, nullptr);
    return buf;
  }
} // namespace weechat_kolmafia

