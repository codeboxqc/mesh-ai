<div align="center" markdown="1">

<img src=".github/meshtastic_logo.png" alt="Meshtastic Logo" width="80"/>
<h1>Meshtastic Firmware custom version ai</h1>

![GitHub release downloads](https://img.shields.io/github/downloads/meshtastic/firmware/total)
[![CI](https://img.shields.io/github/actions/workflow/status/meshtastic/firmware/main_matrix.yml?branch=master&label=actions&logo=github&color=yellow)](https://github.com/meshtastic/firmware/actions/workflows/ci.yml)
[![CLA assistant](https://cla-assistant.io/readme/badge/meshtastic/firmware)](https://cla-assistant.io/meshtastic/firmware)
[![Fiscal Contributors](https://opencollective.com/meshtastic/tiers/badge.svg?label=Fiscal%20Contributors&color=deeppink)](https://opencollective.com/meshtastic/)
[![Vercel](https://img.shields.io/static/v1?label=Powered%20by&message=Vercel&style=flat&logo=vercel&color=000000)](https://vercel.com?utm_source=meshtastic&utm_campaign=oss)

 
</div>

</div>

<div align="center">
	<a href="https://meshtastic.org">Website</a>
	-
	<a href="https://meshtastic.org/docs/">Documentation</a>
</div>



<img width="953" height="1109" alt="asasas" src="https://github.com/user-attachments/assets/471befcf-8908-4bde-b551-26b2f943fb2b" />

telegram preview
https://t.me/ClassNutz




 STEP 1

 Download, unzip


 STEP 2
 using VSCODE
 <img width="1881" height="999" alt="1" src="https://github.com/user-attachments/assets/a7aef8e4-99f3-42a7-8fa2-e10fa07eedff" />


 STEP 3
 <img width="1753" height="989" alt="2" src="https://github.com/user-attachments/assets/a5b2179d-6708-4312-98f9-96980737df78" />


 Next Step
 set your hardware heltec 3 or 4  by default 3
 <img width="1751" height="1131" alt="3" src="https://github.com/user-attachments/assets/2af4efab-28e8-44ee-8295-06bd5cd45ca1" />



 Next Step

edit  add your key 
#define TELEGRAM_BOT_TOKEN ""
#define TELEGRAM_CHAT_ID   ""
#define GROQ_API_KEY       ""
#define GEMINI_API_KEY     ""

https://console.groq.com/ make free key   GROQ_API_KEY        "api key code here"
https://aistudio.google.com/  make free key GEMINI_API_KEY     "api key code here"


#define TELEGRAM_BOT_TOKEN ""
#define TELEGRAM_CHAT_ID   ""
Here is how to generate your Telegram Bot Token and find your personal Chat ID to fill in those variables.1.Create the bot with BotFather :Open the Telegram app and search for @BotFather (the official bot with a verified checkmark). Click "Start" or send the message /start.2.Generate the TELEGRAM_BOT_TOKEN :Send the message /newbot to BotFather.Choose a display name for your bot (e.g., Mesh AI).Choose a unique username that ends in "bot" (e.g., mesh_ai_codeboxqc_bot).BotFather will reply with a long string of characters called the HTTP API Token. Copy this exactly—this is your TELEGRAM_BOT_TOKEN.3.Start your new bot :Required before it can send you messages.Search for your bot's new username in Telegram, open the chat, and click Start (or send /start). The bot cannot message you until you start a conversation with it first.4.Find your TELEGRAM_CHAT_ID :To get the 9 or 10-digit number that represents your personal Telegram account:Search for @userinfobot in Telegram.Click Start (or send /start).The bot will instantly reply with your account details. Look for the Id: 123456789 line.Copy that number—this is your TELEGRAM_CHAT_ID.Once you have both, place them inside the quotes in your C++ code:C++#define TELEGRAM_BOT_TOKEN "1234567890:ABCdefGhIJKlmNoPQRsTUVwxyZ"
#define TELEGRAM_CHAT_ID   "123456789"
Security Note: Just like the Groq and Gemini API keys, never push these strings to your public GitHub repository. You should ideally put them in a .env file or an untracked secrets.h file, and add that file to your .gitignore so your bot cannot be hijacked.


source/module/GRAM.CPP  EDIT THIS ADD YOUR KEY

 <img width="1819" height="1105" alt="aa" src="https://github.com/user-attachments/assets/66cb2adb-7030-413e-89ff-4c6796067cca" />




 Next Step
 plug your heltec in usb
 click compile it will compile and burn your heltec firmware 5 to 15 minutes (speed computer)
 <img width="1884" height="1075" alt="aaz" src="https://github.com/user-attachments/assets/deada4e8-56c8-4b6a-95dd-776c6369a1be" />




 ========================================================
























 Final STEP AI PROMPT 

 source/module/GRAM.CPP   change it to meete your request



 // --- SHARED SYSTEM PROMPT (WITH MANDATORY ENGLISH TRANSLATION RULE) ---
#define AI_SYSTEM_PROMPT \
"You are a mesh-network AI node. Keep every response concise. HARD LIMIT: maximum 180 characters. " \
"Never exceed 180 characters. No explanations outside the answer. " \
"You MUST start EVERY AI response with EXACTLY ONE routing tag: " \
"'!#' for PUBLIC BROADCAST or '!!' for PRIVATE DIRECT MESSAGE. " \
"The tag MUST be the FIRST TWO CHARACTERS of the response. " \
"ROUTING & TRANSLATION RULES:\n" \
"1. NON-ENGLISH = ALWAYS TRANSLATE TO ENGLISH & PUBLIC BROADCAST: If user message contains ANY non-English text or foreign words (Spanish, French, German, Hebrew, Cyrillic, etc.), start response with !# to broadcast publicly AND ALWAYS translate or reply in English. NEVER reply in foreign languages.\n" \
"2. DEFAULT PRIVATE: For normal English conversation, respond privately, starting with !!.\n" \
"3. AI LOCATION: When asked where YOU (the AI node) are located, state that you are a mesh AI node hosted at https://t.me/ClassNutz. NEVER state the sender's location is your own.\n" \
"3.5 If GPS coordinates are provided say heelo to user from is city" \
"5. PUBLIC EXCEPTIONS: Start with !# if user message starts with '!' or '/' at index 0:\n" \
"6. EMPTY/UNCLEAR: If the message is empty, unintelligible, or has no clear request, reply briefly with !! and ask what they need. \n" \
"7. NO HALLUCINATION: Never invent facts, locations, weather, names, URLs, commands, GPS interpretations, or system status. Say 'unknown' when uncertain.\n " \
"8. Never reveal private-message content in a PUBLIC response unless the user explicitly requests public disclosure with !0.\n " \
"9. FINAL CHECK: Before sending, verify: correct tag, English only when required, <=180 characters, no markdown, no duplicate tag, no filler.\n" \
"- !0, /0 <msg>: explicitly force PUBLIC response (PUBLIC)\n" \
"- !1, /1 <msg>: explicitly force PRIVATE response (PRIVATE)\n" \
"Constraints: Under 180 chars. No markdown, no filler."

















=================================================================================
Lack of memory on heltec3  crash often
a better hardware and more memory should be better less crash
===============================================================================




