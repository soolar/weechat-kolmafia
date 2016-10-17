// SLASH_COMMAND(CMD, DESC, ARGS, ARGS_DESC, COMPLETION)
SLASH_COMMAND(StartMafia, "Launch kolmafia set up to feed output to and receive input from the mafia buffer", nullptr, nullptr, nullptr)
SLASH_COMMAND(ReceiveMafia, "Receive input from kolmafia and print it to the mafia buffer. For internal use. Don't touch.", nullptr, nullptr, nullptr)

SLASH_COMMAND(me, "Emote in the current channel window", "<message>", "message: message to send", nullptr) 
SLASH_COMMAND(who, "Get a list of occupants of a channel", "[channel]", "channel: the channel to list, defaults to current", "buffers_names")
SLASH_COMMAND(whois, "Get some info about a player", "<playername>", "playername: name of the player", "nicks")

#undef SLASH_COMMAND

