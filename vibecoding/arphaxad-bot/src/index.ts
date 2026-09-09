import { App } from "@slack/bolt";

const app = new App({
	token: process.env.SLACK_BOT_TOKEN,
	appToken: process.env.SLACK_APP_TOKEN,
	socketMode: true,
});

// Listen for direct messages
app.message(async ({ message, say, logger }) => {
	// Ignore messages from bots and message changes
	if (message.subtype || (message as any).bot_id) {
		return;
	}

	// Only handle DMs
	if (message.channel_type !== "im") {
		return;
	}

	const text = (message as any).text || "";
	logger.info(`Received DM: ${text}`);

	let willReplyOnThread = false;
	if (willReplyOnThread) {
		await say({
			text: `You said: *${text}*`,
			thread_ts: message.ts,
		});
	} else {
		await say({
			text: `Acknowledged: *${text}*`,
		});
	}
});

(async () => {
	await app.start();
	console.log("ArphaXAD is running in socket mode...");
})();
