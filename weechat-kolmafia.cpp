#include "weechat-kolmafia.h"
#include "weechat-kolmafia-config.h"
#include "weechat-kolmafia-channel.h"
#include <string>
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

struct t_weechat_plugin *weechat_plugin = nullptr;
WeechatKolmafia::Plugin *PluginSingleton = nullptr;

#define MAX_PREFIX_LENGTH 4096
#define MAX_TAGS_LENGTH 4096

namespace
{
  int Writer(char *data, size_t size, size_t nmemb, std::string *writerData)
  {
    if(writerData == nullptr)
      return 0;

    writerData->append(data, size*nmemb);

    return size * nmemb;
  }

  struct PrintHtmlCallbackData
  {
    struct t_gui_buffer *buffer;
    time_t when;
    char tags[MAX_TAGS_LENGTH];
    char prefix[MAX_PREFIX_LENGTH];
  };
} // anonymous namespace

int weechat_plugin_init(struct t_weechat_plugin *plugin, int argc, char *argv[])
{
  (void) argc;
  (void) argv;
  weechat_plugin = plugin;
  PluginSingleton = new WeechatKolmafia::Plugin();
  return WEECHAT_RC_OK;
}

int weechat_plugin_end(struct t_weechat_plugin *plugin)
{
  (void) plugin;
  delete PluginSingleton;
  return WEECHAT_RC_OK;
}

namespace WeechatKolmafia
{
  // public
  Plugin::Plugin()
    : conf(new Plugin::Config()),  pollHook(nullptr),  delay(0), distribution(0.0, 1.0), beGood(true)
  {
    PluginSingleton = this;

    curl_global_init(CURL_GLOBAL_ALL);
    lastSeen = "0";

    dbg = weechat_buffer_new("debug", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    events = weechat_buffer_new("events", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    cli = weechat_buffer_new("mafia", InputCliCallback, this, nullptr, CloseCliCallback, this, nullptr);
    weechat_buffer_set(dbg, "notify", "0");

    UpdateSession();
    weechat_command(dbg, "/exec -in kolmafia ashq print(\"@WEECHAT@SESSINIT=true\");");

    SetPollDelay(3000);
    updateNicklistsHook = weechat_hook_timer(1000, 1, 0, UpdateNicklistsCallback, this, nullptr);

#define SLASH_COMMAND(CMD, DESC, ARGS, ARGS_DESC, COMPLETION) weechat_hook_command(#CMD, DESC, ARGS, ARGS_DESC, COMPLETION, Plugin::CMD##_command_aux, this, nullptr);
#include "weechat-kolmafia-commands.h"
  }

  Plugin::~Plugin()
  {
    beGood = false;

    weechat_unhook(pollHook);
    weechat_unhook(updateNicklistsHook);

    delete conf;

    curl_global_cleanup();
  }

  // callbacks
  int Plugin::InputWhisperCallback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *inputData)
  {
    Plugin *plug = (Plugin *) ptr;
    (void) data;
    return plug->HandleInputWhisper(weebuf, inputData);
  }

  int Plugin::InputCliCallback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *inputData)
  {
    Plugin *plug = (Plugin *) ptr;
    (void) data;
    return plug->HandleInputCli(weebuf, inputData);
  }

  int Plugin::CloseWhisperCallback(const void *ptr, void *data, struct t_gui_buffer *weebuf)
  {
    Plugin *plug = (Plugin *) ptr;
    (void) data;
    return plug->HandleCloseWhisper(weebuf);
  }

  int Plugin::CloseCliCallback(const void *ptr, void *data, struct t_gui_buffer *weebuf)
  {
    Plugin *plug = (Plugin *) ptr;
    (void) data;
    return plug->HandleCloseCli(weebuf);
  }

  int Plugin::PollCallback(const void *ptr, void *data, int remainingCalls)
  {
    Plugin *plug = (Plugin *) ptr;
    (void) data;
    (void) remainingCalls;
    return plug->PollMessages();
  }

  int Plugin::UpdateNicklistsCallback(const void *ptr, void *data, int remainingCalls)
  {
    Plugin *plug = (Plugin *) ptr;
    (void) data;
    (void) remainingCalls;
    return plug->UpdateAllNicklists();
  }

  namespace
  {
    struct FontState
    {
      FontState() : bold(false), italic(false), underlined(false), color("default") {}
      void PutColor(std::string &str)
      {
        std::string assembled;
        if(bold) assembled += '*';
        if(italic) assembled += '/';
        if(underlined) assembled += '_';
        assembled += color;
        str += weechat_color(assembled.c_str());
      }
      bool bold;
      bool italic;
      bool underlined;
      std::string color;
    };
  }

  int Plugin::PrintHtmlCallback(const void *ptr, void *dataptr, const char *command, int returnCode,
      const char *out, const char *err)
  {
    (void) ptr;
    if(returnCode > 0 || returnCode == WEECHAT_HOOK_PROCESS_ERROR || out == nullptr)
    {
      weechat_printf(PluginSingleton->dbg, "Error code %d running [%s]: %s",
          returnCode, command, err);
      return WEECHAT_RC_ERROR;
    }

    FontState state;
    PrintHtmlCallbackData *data = (PrintHtmlCallbackData *) dataptr;
    std::string parsed;

    const char *it = out;
    while(*it != '\0')
    {
      if(*it == '\e')
      {
        // escape code go
        ++it;
        if(*it != '\0' && *it == '[')
        {
          ++it;
          std::string currcode;
          // ok it's definitely the right kind
          while(*it != '\0' && *it != 'm')
          {
            currcode += *it;
            ++it;
          }
          if(currcode == "3")
            state.italic = true;
          else if(currcode == "23")
            state.italic = false;
          else if(currcode.size() == 2 && currcode[0] == '3')
          {
            state.color = currcode[1];
          }
          else
            weechat_printf(PluginSingleton->dbg, "Unhandled ansi escape [%s]", currcode.c_str());

          state.PutColor(parsed);
        }
        else
        {
          weechat_printf(PluginSingleton->dbg, "Malformed ansi escape sequence somewhere in {%s}", out);
          return WEECHAT_RC_ERROR;
        }
      }
      else
      {
        if(*it == '\n')
          parsed += weechat_color("reset");
        parsed += *it;
      }
      ++it;
    }

    //weechat_printf(PluginSingleton->dbg, "Here's the situation:\n%s%s", out, parsed.c_str());
    weechat_printf_date_tags(data->buffer, data->when, data->tags,
        "%s%s", data->prefix, parsed.c_str());

    return WEECHAT_RC_OK;
  }

  // commands
#define COMMAND_FUNCTION(CMD) int Plugin::CMD##_command_aux(const void *ptr, void *data, struct t_gui_buffer *weebuf, int argc, char **argv, char **argv_eol) { (void) data; Plugin *plug = (Plugin *) ptr; return plug->CMD##_command(weebuf, argc, argv, argv_eol); } int Plugin::CMD##_command(struct t_gui_buffer *weebuf, int argc, char **argv, char **argv_eol)
  COMMAND_FUNCTION(StartMafia)
  {
    (void) weebuf;
    (void) argc;
    (void) argv;
    (void) argv_eol;

    // TODO: Make sure mafia isn't already running
    weechat_command(dbg, "/exec -sh -stdin -pipe /ReceiveMafia -noln -norc -name kolmafia mafia");
    UpdateSession();
    weechat_command(dbg, "/exec -in kolmafia ashq print(\"@WEECHAT@SESSINIT=true\");");
    return WEECHAT_RC_OK;
  }

  COMMAND_FUNCTION(ReceiveMafia)
  {
    (void) weebuf;
    (void) argv;
    if(argc < 2)
      return WEECHAT_RC_ERROR;

    std::string text(argv_eol[1]);

    if(!text.empty())
    {
      // strip out ANSI codes
      size_t pos = text.find("\e[");
      while(pos != std::string::npos)
      {
        size_t endpos = text.find('m', pos);
        text.erase(pos, endpos - pos + 1);
        pos = text.find("\e[");
      }

      if(text.length() > 1 && text[0] == '>')
        text = text.substr(1);
      if(text.length() > 1 && text[0] == ' ')
        text = text.substr(1);

      std::istringstream ss(weechat_config_string(conf->look.cli_message_blacklist));
      std::string blacklisted;
      while(std::getline(ss, blacklisted, '~'))
      {
        if(text.find(blacklisted) != std::string::npos)
          return WEECHAT_RC_OK;
      }

      static const std::string escapeSequence("@WEECHAT@");
      pos = text.find(escapeSequence);
      if(pos != std::string::npos)
      {
        std::string message(text.substr(pos + escapeSequence.length()));
        //weechat_printf(dbg, "Escape code received from mafia [%s]", message.c_str());
        size_t split = message.find_first_of('=');
        if(split != std::string::npos && split > 0)
        {
          std::string key = message.substr(0, split);
          std::string value = message.substr(split + 1);
          HandleMafiaEscape(key, value);
        }
        return WEECHAT_RC_OK;
      }

      PrintHtml(cli, text);
    }

    return WEECHAT_RC_OK;
  }

  COMMAND_FUNCTION(me)
  {
    if(argc < 2)
      return WEECHAT_RC_ERROR;
    (void) argv;

    std::string message("/");
    message += weechat_buffer_get_string(weebuf, "name");
    message += ' ';
    message += argv_eol[0];
    return PluginSingleton->SubmitMessage(message, weebuf);
  }

  COMMAND_FUNCTION(who)
  {
    (void) argv_eol;

    std::string command("/who ");
    if(argc > 1)
      command += argv[1];
    else
      command += weechat_buffer_get_string(weebuf, "name");
    return PluginSingleton->SubmitMessage(command, weebuf);
  }

  COMMAND_FUNCTION(whois)
  {
    (void) argv_eol;
    if(argc < 2)
      return WEECHAT_RC_ERROR;
    std::string command("/whois ");
    command += argv[1];
    return PluginSingleton->SubmitMessage(command, weebuf);
  }

  // private
  int Plugin::HttpRequest(const std::string &url, std::string &outbuf)
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
    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Writer);
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

  std::string Plugin::HtmlToWeechat(const std::string& html)
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

  void Plugin::HandleMessage(const Json::Value &msg)
  {
    std::string sender = msg["who"]["name"].asString();
    sender += '\t';
    std::string body = msg["msg"].asString();
    time_t when = std::stoul(msg["time"].asString());
    std::string tags("nick_");
    tags += NameUniquify(sender);
    std::string type = msg["type"].asString();

    if(type == "event")
    {
      PrintHtml(events, body, when, "kol_event", "\t");
    }
    else if(type == "system")
    {
      PrintHtml(events, body, when, "kol_system", "\tSystem Message: ");
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
          sender += '\t';
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

      auto chan = GetChannel(channel.c_str());
      chan->WriteMessage(when, sender, body, tags);
    }
    else if(type == "private")
    {
      struct t_gui_buffer *buf = GetWhisperBuffer(sender.c_str());
      PrintHtml(buf, body, when, tags.c_str(), sender.c_str());
    }
    else
    {
      weechat_printf(dbg, "Unrecognized message type \"%s\"", type.c_str());
    }
  }

  void Plugin::PrintHtml(struct t_gui_buffer *buffer, const std::string &html, time_t when /*= 0*/,
      const char *tags /*= nullptr*/, const char *prefix /*= nullptr*/)
  {
    std::string command("~/Sandbox/weechat-kolmafia/parse-html.sh '");
    for(auto it = html.begin(); it != html.end(); ++it)
    {
      if(*it == '\'')
        command += '\\';

      if(*it != '\n')
        command += *it;
      else
        command += ' ';
    }
    command += '\'';
    PrintHtmlCallbackData *data = (PrintHtmlCallbackData *) malloc(sizeof(PrintHtmlCallbackData));
    data->buffer = buffer;
    data->when = when;
    // TODO: length checking for safety's sake
    if(tags != nullptr)
      std::strcpy(data->tags, tags);
    else
      data->tags[0] = '\0';
    if(prefix != nullptr)
      std::strcpy(data->prefix, prefix);
    else
      data->prefix[0] = '\0';
    weechat_printf(dbg, "\t%s", command.c_str());
    weechat_hook_process(command.c_str(), 30000, PrintHtmlCallback, nullptr, data);
  }

  void Plugin::UpdateSession()
  {
    weechat_command(dbg, "/exec -in kolmafia ashq print(\"@WEECHAT@HASH=\" + my_hash()); "
        "print(\"@WEECHAT@ID=\" + my_id()); print(\"@WEECHAT@NAME=\" + my_name());");
  }

  std::string Plugin::UrlEncode(const std::string &text)
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

  std::string Plugin::NameUniquify(const std::string &name)
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
    nameDeuniquifies[encoded] = name;
    return encoded;
  }

  std::string Plugin::NameDeuniquify(const std::string &name)
  {
    return nameDeuniquifies[name];
  }

  int Plugin::SubmitMessage(const std::string &message, std::string &outbuf)
  {
    UpdateSession();

    std::string url(URL("submitnewchat.php?playerid="));
    url += std::to_string(weechat_config_integer(conf->session.playerid));
    url += "&pwd=";
    url += weechat_config_string(conf->session.hash);
    url += "&graf=";
    url += UrlEncode(message);
    url += "&j=1";

    if(HttpRequest(url, outbuf) != WEECHAT_RC_OK)
      return WEECHAT_RC_ERROR;

    return WEECHAT_RC_OK;
  }

  int Plugin::SubmitMessage(const std::string &message, struct t_gui_buffer *buffer)
  {
    std::string res;
    if(SubmitMessage(message, res) == WEECHAT_RC_ERROR)
      return WEECHAT_RC_ERROR;

    //weechat_printf(dbg,"%s",buffer.c_str());
    Json::Value v;
    Json::Reader r;
    r.parse(res, v);
    std::string output = v["output"].asString();
    if(!output.empty())
      PrintHtml(buffer, output);
    return WEECHAT_RC_OK;
  }

  int Plugin::HandleInputWhisper(struct t_gui_buffer *weebuf, const char *inputData)
  {
    std::string message("/msg ");
    message += weechat_buffer_get_string(weebuf, "name");
    message += " ";
    message += inputData;

    weechat_printf(weebuf, "%s\t%s", weechat_config_string(conf->session.playername), inputData);

    std::string buffer;
    return SubmitMessage(message, buffer);
  }

  int Plugin::HandleInputCli(struct t_gui_buffer *weebuf, const char *inputData)
  {
    std::string command("/exec -in kolmafia ");
    command += inputData;
    weechat_printf(weebuf, "\t%s> %s", weechat_color("green"), inputData);
    return weechat_command(weebuf, command.c_str());
  }

  int Plugin::HandleCloseWhisper(struct t_gui_buffer *weebuf)
  {
    whispers.erase(NameUniquify(weechat_buffer_get_string(weebuf, "name")));
    return WEECHAT_RC_OK;
  }

  int Plugin::HandleCloseCli(struct t_gui_buffer *weebuf)
  {
    (void) weebuf;
    return WEECHAT_RC_OK;
  }

  void Plugin::HandleMafiaEscape(const std::string &key, const std::string &value)
  {
    if(key == "HASH")
      weechat_config_option_set(conf->session.hash, value.c_str(), 0);
    else if(key == "ID")
      weechat_config_option_set(conf->session.playerid, value.c_str(), 0);
    else if(key == "NAME")
      weechat_config_option_set(conf->session.playername, value.c_str(), 0);
    else if(key == "SESSINIT")
    {
      // open buffers for all currently listened channels
      std::string res;
      if(SubmitMessage("/l", res) != WEECHAT_RC_ERROR)
      {
        Json::Value v;
        Json::Reader r;
        r.parse(res, v);
        std::istringstream ss(HtmlToWeechat(v["output"].asString()));
        while(std::getline(ss, res))
        {
          if(res.size() > 2 && res[0] == ' ' && res[1] == ' ')
          {
            GetChannel(res.substr(2));
          }
        }
      }
    }
    else
      weechat_printf(dbg, "Unknown mafia escape [%s=%s]", key.c_str(), value.c_str());
  }

  int Plugin::PollMessages()
  {
    std::string url(URL("newchatmessages.php?aa="));
    url += std::to_string(distribution(generator));
    url += "&j=1&lasttime=";
    url += lastSeen;
    std::string buffer;
    if(HttpRequest(url, buffer) != WEECHAT_RC_OK)
      return WEECHAT_RC_ERROR;

    //weechat_printf(dbg, "%s", buffer.c_str());
    Json::Value v;
    Json::Reader r;
    r.parse(buffer, v);
    Json::Value msgs = v["msgs"];

    lastSeen = v["last"].asString();
    SetPollDelay(v["delay"].asInt64());

    for(Json::ArrayIndex i = 0; i < msgs.size(); ++i)
    {
      HandleMessage(msgs[i]);
    }

    return WEECHAT_RC_OK;
  }

  int Plugin::UpdateAllNicklists()
  {
    for(auto it = channels.begin(); it != channels.end(); ++it)
    {
      it->second->UpdateNicklist();
    }

    return WEECHAT_RC_OK;
  }

  int Plugin::SetPollDelay(long newDelay)
  {
    if(delay == newDelay)
      return WEECHAT_RC_OK;

    if(pollHook != nullptr)
      weechat_unhook(pollHook);

    delay = newDelay;
    pollHook = weechat_hook_timer(newDelay, 1, 0, PollCallback, this, nullptr);

    return WEECHAT_RC_OK;
  }

  Plugin::Channel *Plugin::GetChannel(const std::string &name)
  {
    auto it = channels.find(name);
    if(it == channels.end())
    {
      auto p = channels.insert(std::make_pair(name, new Channel(name)));
      return p.first->second;
    }
    return it->second;
  }

  struct t_gui_buffer *Plugin::GetWhisperBuffer(const std::string &name)
  {
    std::string encoded = NameUniquify(name);
    struct t_gui_buffer *buf = weechat_buffer_search("kol", encoded.c_str());
    if(buf == nullptr)
      buf = weechat_buffer_new(encoded.c_str(), InputWhisperCallback, this, nullptr, CloseWhisperCallback, this, nullptr);
    whispers[name] = buf;
    return buf;
  }
} // namespace WeechatKolmafia

