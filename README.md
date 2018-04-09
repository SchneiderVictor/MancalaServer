# MancalaServer
An assignment: Creates a server that hosts a Mancala game session

# Usage
For simplicity, this usage is only for playing the game while connected to the server machine (from the command line)

From the server, simply compile mancsrv.c then run mancsrv with the -p option (given a port number of your choice)

>$ gcc -std=gnu99 -o mancsrv mancsrv.c

>$ ./mancsrv -p port

From the client, made sure you are connected to some terminal/shell on the server. From there call (using the same port used on the server side):

>$ nc 127.0.0.1 port

# Rules
Each player begins with four pebbles in each regular pit, and an empty end pit.

On your turn, you choose any non-empty pit on your side of the board (not including the end pit), and pick up all of the pebbles from that pit and distribute them to the right: one pebble in the next pit to the right, another pebble into the next pit to the right after that; and so on until you've distributed all of them. You might manage to put a pebble into your end pit. If you go beyond your end pit, that's fine, you put pebbles into other people's pits. However, you always skip other people's end pits.

If you end your turn by putting a pebble into your own end pit (i.e., if it works out exactly), then you get another turn. There is no limit to how many consecutive turns you can get by this method.

After that, it's another player's turn. The game ends when any one player's side is empty. At the end of the game, each player's score is all of the pebbles remaining on their side (which will consist mostly of the end pit, and will consist exclusively of the end pit for the player who emptied their side).

If you end your turn by putting a pebble into a non-end pit on your own side which was formerly empty, there's another rule which gets you some of your opponent's pebbles at that point, but this rule was ignored for this assignment.

The server itself will handle the playthrough and provide appropriate instructions.

Learn more at: http://play-mancala.com
