#include "weechat-kolmafia.h"
#include "weechat-kolmafia-config.h"
#include "weechat-kolmafia-channel.h"
#include <string>
#include <time.h>
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
    : conf(new Plugin::Config()), kolmafiaHook(nullptr), kolmafiaRunning(false), pollHook(nullptr),
      delay(0), distribution(0.0, 1.0), beGood(true)
  {
    PluginSingleton = this;

    lastSeen = "0";

    dbg = weechat_buffer_new("debug", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    events = weechat_buffer_new("events", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    cli = weechat_buffer_new("mafia", InputCliCallback, this, nullptr, CloseCliCallback, this, nullptr);
    weechat_buffer_set(dbg, "notify", "0");

    StartMafia_command(nullptr, 0, nullptr, nullptr);

    SetPollDelay(3000);
    updateNicklistsHook = weechat_hook_timer(1000, 1, 0, UpdateNicklistsCallback, this, nullptr);

#define SLASH_COMMAND(CMD, DESC, ARGS, ARGS_DESC, COMPLETION) weechat_hook_command(#CMD, DESC, ARGS, ARGS_DESC, COMPLETION, Plugin::CMD##_command_aux, this, nullptr);
#include "weechat-kolmafia-commands.h"
  }

  Plugin::~Plugin()
  {
    ProvideMafiaInput("quit");
    beGood = false;

    weechat_unhook_all(nullptr);

    delete conf;
  }

  // callbacks
  int Plugin::InputWhisperCallback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *inputData)
  {
    if(!PluginSingleton->beGood)
      return WEECHAT_RC_ERROR;
    Plugin *plug = (Plugin *) ptr;
    (void) data;
    return plug->HandleInputWhisper(weebuf, inputData);
  }

  int Plugin::InputCliCallback(const void *ptr, void *data, struct t_gui_buffer *weebuf, const char *inputData)
  {
    if(!PluginSingleton->beGood)
      return WEECHAT_RC_ERROR;
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
    if(!PluginSingleton->beGood)
      return WEECHAT_RC_ERROR;
    Plugin *plug = (Plugin *) ptr;
    (void) data;
    (void) remainingCalls;
    return plug->PollMessages();
  }

  int Plugin::UpdateNicklistsCallback(const void *ptr, void *data, int remainingCalls)
  {
    if(!PluginSingleton->beGood)
      return WEECHAT_RC_ERROR;
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
    if(!PluginSingleton->beGood)
      return WEECHAT_RC_ERROR;
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
        "%s\t%s", data->prefix, parsed.c_str());

    return WEECHAT_RC_OK;
  }

  // commands
#define COMMAND_FUNCTION(CMD) int Plugin::CMD##_command_aux(const void *ptr, void *data, struct t_gui_buffer *weebuf, int argc, char **argv, char **argv_eol) { (void) data; Plugin *plug = (Plugin *) ptr; return plug->CMD##_command(weebuf, argc, argv, argv_eol); } int Plugin::CMD##_command(struct t_gui_buffer *weebuf, int argc, char **argv, char **argv_eol)
  COMMAND_FUNCTION(StartMafia)
  {
    (void) argc;
    (void) argv;
    (void) argv_eol;

    if(kolmafiaRunning)
    {
      weechat_printf(weebuf, "Mafia is already running, ya dingus");
      return WEECHAT_RC_ERROR;
    }
    struct t_hashtable *options = weechat_hashtable_new(8, WEECHAT_HASHTABLE_STRING,
        WEECHAT_HASHTABLE_STRING, nullptr, nullptr);
    weechat_hashtable_set(options, "stdin", "1");
    weechat_hashtable_set(options, "buffer_flush", "1");
    kolmafiaHook = weechat_hook_process_hashtable("mafia", options, 0,
        MafiaOutputAvailableCallback, nullptr, nullptr);
    weechat_hashtable_free(options);
    kolmafiaRunning = true;
    UpdateSession();
    ProvideMafiaInput("ashq print(\"@WEECHAT@SESSINIT=true\");");
    return WEECHAT_RC_OK;
  }

  int Plugin::MafiaOutputAvailableCallback(const void *ptr, void *data, const char *command,
      int returnCode, const char *out, const char *err)
  {
    (void) err; // TODO: check dis
    (void) ptr;
    (void) data;
    (void) command;
    if(returnCode >= 0)
    {
      PluginSingleton->kolmafiaRunning = false;
      return WEECHAT_RC_OK;
    }

    if(out == nullptr)
      return WEECHAT_RC_ERROR;

    std::istringstream is(out);
    std::string text;
    while(std::getline(is, text))
    {
      while(text.length() > 1 && text[0] == ' ')
        text = text.substr(1);

      if(!text.empty() && text != "> ")
      {
        std::istringstream ss(weechat_config_string(PluginSingleton->conf->look.cli_message_blacklist));
        std::string blacklisted;
        while(std::getline(ss, blacklisted, '~'))
        {
          if(text.find(blacklisted) != std::string::npos)
            return WEECHAT_RC_OK;
        }

        static const std::string escapeSequence("@WEECHAT@");
        size_t pos = text.find(escapeSequence);
        if(pos != std::string::npos)
        {
          std::string message(text.substr(pos + escapeSequence.length()));
          //weechat_printf(dbg, "Escape code received from mafia [%s]", message.c_str());
          size_t split = message.find_first_of('=');
          if(split != std::string::npos && split > 0)
          {
            std::string key = message.substr(0, split);
            std::string value = message.substr(split + 1);
            PluginSingleton->HandleMafiaEscape(key, value);
          }
          return WEECHAT_RC_OK;
        }

        PluginSingleton->PrintHtml(PluginSingleton->cli, text);
      }
    }

    return WEECHAT_RC_OK;
  }

  int Plugin::ProvideMafiaInput(const std::string &str)
  {
    weechat_printf(dbg, "kolmafia input %s", str.c_str());
    if(!kolmafiaRunning)
      return WEECHAT_RC_ERROR;
    std::string input(str + '\n');
    weechat_hook_set(kolmafiaHook, "stdin", input.c_str());
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
  int Plugin::HttpRequest(const std::string &url, HttpRequestCallback callback,
      const void *ptr /*= nullptr*/, void *data /*= nullptr*/)
  {
    if(!beGood || !kolmafiaRunning)
      return WEECHAT_RC_ERROR;
    //weechat_printf(dbg, "GET %s", url.c_str());

    struct t_hashtable *options = weechat_hashtable_new(
        16, WEECHAT_HASHTABLE_STRING, WEECHAT_HASHTABLE_STRING, nullptr, nullptr);
    if(options == nullptr)
      return WEECHAT_RC_ERROR;
    weechat_hashtable_set(options, "timeout_ms", "30000");
    weechat_hashtable_set(options, "httpheader",
        "Connection: keep-alive\n"
        "Accept: application/json, text/javascript, */*; q=0.01\n"
        "X-Requested-With: XMLHttpRequest\n"
        "Accept-Language: en-US,en;q=0.8");
    weechat_hashtable_set(options, "useragent", "Mozilla/5.0 (X11; Linux x86_64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/53.0.2785.116 Safari/537.36");
    weechat_hashtable_set(options, "referer", "http://127.0.0.1:60080/mchat.php");
    weechat_hashtable_set(options, "accept_encoding", "gzip, deflate, sdch");

    std::string process("url:");
    process += url;
    weechat_hook_process_hashtable(process.c_str(), options, 30000, callback, ptr, data);
    weechat_hashtable_free(options);

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
    std::string body = msg["msg"].asString();
    time_t when;
    try
    {
      when = std::stoul(msg["time"].asString());
    }
    catch (std::exception)
    {
      when = 0;
    }
    std::string tags("nick_");
    tags += NameUniquify(sender);
    std::string type = msg["type"].asString();

    if(type == "event")
    {
      PrintHtml(events, body, when, "kol_event", nullptr);
    }
    else if(type == "system")
    {
      body.insert(0, "System Message: ");
      PrintHtml(events, body, when, "kol_system", nullptr);
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
        command += "'\"'\"'";
      else if(*it == '\n')
        command += ' ';
      else
        command += *it;
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
    //weechat_printf(dbg, "\t%s", command.c_str());
    weechat_hook_process(command.c_str(), 30000, PrintHtmlCallback, nullptr, data);
  }

  void Plugin::UpdateSession()
  {
    ProvideMafiaInput("ashq "
        "print(\"@WEECHAT@HASH=\" + my_hash()); "
        "print(\"@WEECHAT@ID=\" + my_id()); "
        "print(\"@WEECHAT@NAME=\" + my_name());");
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

  int Plugin::SubmitMessage(const std::string &message, HttpRequestCallback callback,
      const void *ptr /*= nullptr*/, void *data /*= nullptr*/)
  {
    UpdateSession();

    std::string url(URL("submitnewchat.php?playerid="));
    url += std::to_string(weechat_config_integer(conf->session.playerid));
    url += "&pwd=";
    url += weechat_config_string(conf->session.hash);
    url += "&graf=";
    url += UrlEncode(message);
    url += "&j=1";

    if(HttpRequest(url, callback, ptr, data) != WEECHAT_RC_OK)
      return WEECHAT_RC_ERROR;

    return WEECHAT_RC_OK;
  }

  int Plugin::SubmitGenericCallback(const void *ptr, void *data,
      const char *command, int returnCode, const char *out, const char *err)
  {
    if(!PluginSingleton->beGood)
      return WEECHAT_RC_ERROR;
    (void) data;
    (void) command;
    (void) returnCode; // TODO: Look at this
    (void) err; // TODO: also this
    if(out == nullptr) return WEECHAT_RC_ERROR;
    struct t_gui_buffer *buffer = (struct t_gui_buffer *) ptr;
    Json::Value v;
    Json::Reader r;
    r.parse(out, v);
    std::string output = v["output"].asString();
    if(!output.empty())
      PluginSingleton->PrintHtml(buffer, output);
    return WEECHAT_RC_OK;
  }

  int Plugin::SubmitMessage(const std::string &message, struct t_gui_buffer *buffer)
  {
    if(!kolmafiaRunning)
    {
      weechat_printf(buffer, "Can't very well submit something if mafia isn't running, can we?");
      return WEECHAT_RC_ERROR;
    }
    return SubmitMessage(message, SubmitGenericCallback, buffer, nullptr);
  }

  int Plugin::HandleInputWhisper(struct t_gui_buffer *weebuf, const char *inputData)
  {
    if(!kolmafiaRunning)
    {
      weechat_printf(weebuf, "Start kolmafia if you want to chat");
      return WEECHAT_RC_ERROR;
    }
    std::string message("/msg ");
    message += weechat_buffer_get_string(weebuf, "name");
    message += " ";
    message += inputData;

    weechat_printf(weebuf, "%s\t%s", weechat_config_string(conf->session.playername), inputData);

    return SubmitMessage(message, weebuf);
  }

  int Plugin::HandleInputCli(struct t_gui_buffer *weebuf, const char *inputData)
  {
    weechat_printf(weebuf, "\t%s> %s", weechat_color("green"), inputData);
    return ProvideMafiaInput(inputData);
  }

  int Plugin::HandleCloseWhisper(struct t_gui_buffer *weebuf)
  {
    (void) weebuf;
    return WEECHAT_RC_OK;
  }

  int Plugin::HandleCloseCli(struct t_gui_buffer *weebuf)
  {
    (void) weebuf;
    return WEECHAT_RC_OK;
  }

  int Plugin::SessInitParseListensCallback(const void *ptr, void *data,
      const char *command, int returnCode, const char *out, const char *err)
  {
    (void) ptr;
    (void) data;
    (void) command;
    (void) returnCode;
    (void) err;

    if(out == nullptr) return WEECHAT_RC_ERROR;
    Json::Value v;
    Json::Reader r;
    r.parse(out, v);
    std::istringstream ss(PluginSingleton->HtmlToWeechat(v["output"].asString()));
    std::string line;
    while(std::getline(ss, line))
    {
      if(line.size() > 2 && line[0] == ' ' && line[1] == ' ')
      {
        PluginSingleton->GetChannel(line.substr(2));
      }
    }
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
      SubmitMessage("/l", SessInitParseListensCallback);
    }
    else
      weechat_printf(dbg, "Unknown mafia escape [%s=%s]", key.c_str(), value.c_str());
  }

  int Plugin::PollHandlingCallback(const void *ptr, void *data,
      const char *command, int returnCode, const char *out, const char *err)
  {
    if(!PluginSingleton->beGood)
      return WEECHAT_RC_ERROR;

    (void) ptr;
    (void) data;
    (void) command;
    (void) returnCode;
    (void) err; // TODO: Check this and returnCode

    if(out == nullptr) return WEECHAT_RC_ERROR;
    Json::Value v;
    Json::Reader r;
    r.parse(out, v);
    Json::Value msgs = v["msgs"];

    PluginSingleton->lastSeen = v["last"].asString();
    PluginSingleton->SetPollDelay(v["delay"].asInt64());

    for(Json::ArrayIndex i = 0; i < msgs.size(); ++i)
    {
      PluginSingleton->HandleMessage(msgs[i]);
    }

    return WEECHAT_RC_OK;
  }

  int Plugin::PollMessages()
  {
    std::string url(URL("newchatmessages.php?aa="));
    url += std::to_string(distribution(generator));
    url += "&j=1&lasttime=";
    url += lastSeen;
    return HttpRequest(url, PollHandlingCallback);
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
    return buf;
  }
} // namespace WeechatKolmafia

