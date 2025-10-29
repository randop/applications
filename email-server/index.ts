import { SMTPServer } from "smtp-server";
import { v7 as uuidv7 } from "uuid";
import { readFileSync } from "node:fs";
import { writeFile } from "node:fs/promises";
import {
  SMTP_551_USERNOTLOCALPLEASETTRY,
  SMTP_552_EXCEEDEDSTORAGEALLOCATION,
  SMTP_553_MAILBOXNAMENOTALLOWED,
  SMTP_554_TRANSACTIONFAILED,
} from "./response-codes";

import SMTP_RESPONSE_CODES from "./response-codes";

type SMTPResponseCode =
  typeof SMTP_RESPONSE_CODES[keyof typeof SMTP_RESPONSE_CODES];

interface ExtendedError extends Error {
  responseCode: SMTPResponseCode;
}

const log = console;
const EXIT_SUCCESS: number = 0;

let verifyAllowLocalhost: boolean = false;
let verifyServerName: boolean = false;
let verifyMailFrom: boolean = false;
let verifyMailTo: boolean = false;
let useProxy: boolean = false;
let disableReverseLookup: boolean = true;
let allowInsecureAuth: boolean = true;
let logger: boolean = true;

const emailDirectory: string = "/emails/new/";
const certFile: string = "/emails/certs/cert.pem";
const sslKeyFile: string = "/emails/certs/key.pem";

const options: any = {
  key: readFileSync(sslKeyFile),
  cert: readFileSync(certFile),
  banner: "Ready for emails",
  logger,
  secure: false,
  allowInsecureAuth,
  name: "EmailServerSandbox",
  size: 5242880, // 5MB
  disabledCommands: [],
  authMethods: ["PLAIN", "LOGIN", "CRAM-MD5", "XOAUTH2"],
  authOptional: true,
  maxClients: 30,
  useProxy,
  useXClient: true,
  hidePIPELINING: true,
  useXForward: true,
  disableReverseLookup,
  socketTimeout: 30000,
  closeTimeout: 15000,
  onAuth(auth, session, callback) {
    // auth.method → 'PLAIN', 'LOGIN', 'XOAUTH2', or 'CRAM-MD5'
    // Return `callback(err)` to reject, `callback(null, response)` to accept
    let response: any = {};
    callback(null, { user: auth.username });
  },
  onSecure(socket, session, callback) {
    if (verifyServerName && session.servername !== "mail.quizbin.com") {
      return callback(new Error("SNI mismatch"));
    }
    callback();
  },
  onConnect(session, callback) {
    if (verifyAllowLocalhost && session.remoteAddress === "127.0.0.1") {
      return callback(new Error("Connections from localhost are forbidden"));
    }
    callback(); // accept
  },
  onClose(session) {
    //console.log(`Connection from ${session.remoteAddress} closed`);
  },
  onMailFrom(address, session, callback) {
    if (verifyMailFrom && !address.address.endsWith("@quizbin.com")) {
      return callback(
        Object.assign(new Error("Relay denied"), {
          responseCode: SMTP_553_MAILBOXNAMENOTALLOWED,
        }),
      );
    }
    callback();
  },
  onRcptTo(address, session, callback) {
    if (verifyMailTo && address.address === "@quizbin.com") {
      return callback(
        Object.assign(new Error(`User ${address.address} is unknown`), {
          responseCode: SMTP_551_USERNOTLOCALPLEASETTRY,
        }),
      );
    }
    callback();
  },
  onData(stream, session, callback) {
    let data: string = "";
    stream.on("data", (chunk: Buffer) => {
      data += chunk.toString("utf8");
    });
    stream.on("end", async () => {
      if (stream.sizeExceeded) {
        const err = Object.assign(new Error("Message is too large"), {
          responseCode: SMTP_552_EXCEEDEDSTORAGEALLOCATION,
        });
        return callback(err);
      } else {
        const uuid: string = uuidv7();
        let emailFile = `${emailDirectory}${uuid}`;
        try {
          await writeFile(emailFile, data, "utf8");
        } catch (err) {
          log.error(`Error writing email file: ${emailFile}`);
        }
      }
      callback(null, "OK");
    });
  },
};

log.info(`Using email directory: ${emailDirectory}`);

const server = new SMTPServer(options);

const port: number = 2525;
const host: string = "0.0.0.0";

server.listen(port, host, () => {
  log.info(`SMTP server ${host} listening on port ${port}`);
});

server.on("error", (err) => {
  log.error("SMTP Server error:", err.message);
});

// Graceful shutdown
process.on("SIGINT", () => {
  log.info("Shutting down SMTP server...");
  server.close(() => {
    process.exit(EXIT_SUCCESS);
  });
});
