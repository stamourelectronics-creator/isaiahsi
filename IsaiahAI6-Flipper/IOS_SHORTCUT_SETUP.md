# IsaiahChat iPhone Siri/Shortcuts bridge

This bridge launches Apple's documented Shortcuts URL scheme over the Flipper's Bluetooth HID keyboard. It does not grant the Flipper direct access to iMessage, SMS history, iPhone contacts, or notification contents. Set up the shortcuts below once on the iPhone.

## 1. Create “IsaiahChat Send”

1. Open **Shortcuts** on the iPhone and tap **+** to make a shortcut.
2. Name it exactly **IsaiahChat Send** (the name must match the Flipper menu).
3. Add the **Send Message** action.
4. Set the message body to **Shortcut Input** (the text sent by IsaiahChat).
5. Set the recipient to **Ask Each Time** so iOS asks you who to message.
6. Keep any send confirmation that iOS requires. Test the shortcut from Shortcuts before using it from the Flipper.

When you select **Siri: Send Message**, type the message on the Flipper. Before confirming, focus Safari's address bar (or another iPhone text field that accepts a URL), then submit the Flipper input. IsaiahChat types a URL of the form `shortcuts://run-shortcut?name=IsaiahChat%20Send&input=text&text=...` and presses Return. If iOS blocks the hand-off, unlock the phone and run the shortcut directly from Shortcuts.

## 2. Create “IsaiahChat Read”

1. Create another shortcut named exactly **IsaiahChat Read**.
2. Add **Find Messages**. Sort by **Date**, newest first, and limit the results to **1**.
3. Add an action to get the message body/text from the found message (if the action exposes message details separately).
4. Add **Speak Text** using that body/text. You can also add **Show Result** if you want the text on the iPhone screen.
5. Run it manually once and approve any iOS permissions/prompts.

Selecting **Siri: Read Latest** launches this shortcut. If configured and permitted by iOS, your iPhone will speak the latest message; the message content is not transferred back to the Flipper display.

## 3. Pair the Flipper

1. In IsaiahChat choose **Connect iPhone**.
2. On the iPhone, open **Settings → Bluetooth**, find the Flipper keyboard/HID device, and pair it.
3. To launch a shortcut, have an appropriate text/URL field focused on the iPhone. A Bluetooth keyboard cannot silently operate the iPhone while it is locked, and iOS can still ask for confirmation.

## Limitations

- iOS does not provide ordinary third-party apps or generic HID keyboards unrestricted access to the Messages database or contacts.
- AirPods' Announce Messages feature is an Apple-integrated iOS capability. For background incoming announcements, enable **Settings → Notifications → Announce Notifications / Announce Messages with Siri** (the exact menu varies by iOS version) and choose Messages.
- This bridge can start a shortcut and pass typed text into it. It cannot guarantee background/locked-screen sending, bypass iOS confirmation, or display incoming replies on the Flipper. A true live, two-way text inbox on the Flipper would require an iPhone companion app using only iOS-supported APIs and still would not have unrestricted iMessage access.
