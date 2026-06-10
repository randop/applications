import dgram from "dgram";
import dns from "dns";
import { promisify } from "util";

const log = console;

const DEFAULT_NTP_PORT: number = 123;
const NTP_PACKET_SIZE: number = 48;
 
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
    let ntpData = Buffer.alloc(NTP_PACKET_SIZE);
    const client = dgram.createSocket("udp4");
    const sendAsync = promisify(client.send.bind(client));
    try {
      await sendAsync(message, this.ntpHost.port, this.ntpHost.address);
      log.debug(`Sent: ${message.toString('hex')}`);
 
      ntpData = await new Promise<Buffer>((resolve, reject) => {
        client.on("message", (message, info) => {
          log.debug(`Received from ${info.address}:${info.port}`);
          resolve(message);
        });
 
        client.on("error", reject);
      });
    } catch (err) {
      log.error(`{ntp.send} [ERROR] ${err.message}`);
    } finally {
      client.close();
    }

    return ntpData;
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
