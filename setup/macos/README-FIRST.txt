Def Jam: Fight for NY - Recompiled: setup for Mac (Apple Silicon, macOS 11 or newer)

Setup builds the game on your Mac from your own USA Xbox dump. It contains no game
data and no game code, and nothing from your dump leaves your computer.

1. Drag "Def Jam Setup" to your Applications folder (or anywhere you like) and open it.

   macOS will say it "can't be opened" or "could not verify" it. This is expected: the
   setup is not signed with an Apple Developer ID. Choose Done (not Move to Trash), then:

     System Settings > Privacy & Security > scroll down to Security >
     "Def Jam Setup was blocked..." > Open Anyway

   and confirm with your password or Touch ID. You only have to do this once.

2. Setup needs the Xcode Command Line Tools (Apple's free compiler tools). If they are
   missing, setup stops and tells you. Apple's instructions:
   https://developer.apple.com/documentation/xcode/installing-the-command-line-tools/

3. Choose your dump (an ISO/XISO image or an extracted folder), check the folders, and
   select Install / Repair. The first run takes about an hour and needs no internet.
   Keep about 12 GB free.

4. When it finishes, select Play, or open "Def Jam Recompiled" from the install folder,
   your Applications folder or the Desktop.

To update or repair, run a newer setup with the same folders. Saves and settings stay in
the data folder. If something goes wrong, "Open logs" shows the log of the current run.
Review logs for personal paths before sharing them.

Third-party licenses are in the Licenses folder.
