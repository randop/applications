import dgram from "dgram";
import dns from "dns";
import { promisify } from "util";

const log = console;

const DEFAULT_NTP_PORT: number = 123;
const NTP_PACKET_SIZE: number = 48;
const TIMEOUT_MS: number = 10_000;
 
interface NtpHost {
  host: string;
  address: string;
  port: number;
}
 
class NtpClient {
  private ntpHost: NtpHost;

  constructor(host: string, port: number) {
    this.ntpHost = { host, address: "", port};
  }

  async resolve(): Promise<void> {
    const { address } = await dns.promises.lookup(this.ntpHost.host, 4);
    this.ntpHost.address = address;
    log.debug(`{ntp.resolve} ${this.ntpHost.host} -> ${this.ntpHost.address}:${this.ntpHost.port}`);
  }

  async send(message: Buffer): Promise<Buffer> {
     return new Promise((resolve, reject) => {
      const client = dgram.createSocket("udp4");

      const timeout = setTimeout(() => {
        client.close();
        reject(new Error(`{ntp} [ERROR] operation timeout`));
      }, TIMEOUT_MS);
 
      const cleanup = () => clearTimeout(timeout);

      client.on("message", (message, info) => {
        log.debug(`{ntp} received from ${info.address}:${info.port}`);
        client.close();
        resolve(message);
      });
 
      client.on("error", (err) => {
        cleanup();
        log.error(`{ntp} [ERROR] request: ${err.message}`);
        client.close();
        reject(err);
      });
 
      client.on("close", () => {
        log.debug(`{ntp} client closed`);
      });
 
      client.send(message, this.ntpHost.port, this.ntpHost.address, (err) => {
        if (err) {
          cleanup();
          client.close();
          reject(err);
        } else {
          log.debug(`{ntp} request sent`);
        }
      });
    });
  }
}

async function main(): Promise<void> {
  const client = new NtpClient("pool.ntp.org", DEFAULT_NTP_PORT);
  await client.resolve();
  const ntpData = Buffer.alloc(NTP_PACKET_SIZE);
  ntpData[0] = 0x1b;
  const ntpResult = await client.send(ntpData);
  log.debug(`{ntp.result} ${ntpResult.toString('hex')}`);
}

main().catch((err) => {
  log.error(`{main} [ERROR] ${err.message}`);
  process.exit(1);
});
