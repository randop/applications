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
        cleanup();
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
  let ntpResult = await client.send(ntpData);
  log.debug(`{ntp.result} ${ntpResult.toString('hex')}`);

  const leapIndicator = (ntpResult[0] >> 6) & 0x03;
  const versionNumber = (ntpResult[0] >> 3) & 0x07;
  const mode = ntpResult[0] & 0x07;
  log.info(`Leap Indicator: ${leapIndicator}`);
  log.info(`Version Number: ${versionNumber}`);
  log.info(`Mode: ${mode}`);

  const stratum = ntpResult[1];
  log.info(`Stratum: ${stratum}`);

  const pool = ntpResult[2];
  log.info(`Pool: ${pool}`);

  // signed: -128 to 127
  const precision = ntpResult[3] > 127 ? ntpResult[3] - 256 : ntpResult[3];
  log.info(`Precision: ${precision}`);

  // 32 bits (fixed-point)
  const rootDelayFixedPoint = (ntpResult[4] << 24 | ntpResult[5] << 16 | ntpResult[6] << 8 | ntpResult[7]) >>> 0;
  const rootDelay = (rootDelayFixedPoint & 0xFFFF) / 65536;
  log.info(`Root Delay: ${rootDelay} (seconds)`);

  const rootDispersionRaw = (ntpResult[8] << 24 | ntpResult[9] << 16 | ntpResult[10] << 8 | ntpResult[11]) >>> 0;
  const rootDispersion = (rootDispersionRaw & 0xFFFF) / 65536;
  log.info(`Root Dispersion: ${rootDispersion} (seconds)`);

  const referenceId = Buffer.alloc(4);
  referenceId[0] = ntpResult[12];
  referenceId[1] = ntpResult[13];
  referenceId[2] = ntpResult[14];
  referenceId[3] = ntpResult[15];
  log.info(`Reference ID: ${referenceId.toString('hex')}`);

  const referenceTimestampRaw = Buffer.alloc(8);
  referenceTimestampRaw[0] = ntpResult[16];
  referenceTimestampRaw[1] = ntpResult[17];
  referenceTimestampRaw[2] = ntpResult[18];
  referenceTimestampRaw[3] = ntpResult[19];
  referenceTimestampRaw[4] = ntpResult[20];
  referenceTimestampRaw[5] = ntpResult[21];
  referenceTimestampRaw[6] = ntpResult[22];
  referenceTimestampRaw[7] = ntpResult[23];
  log.info(`Reference Timestamp: (0x${referenceTimestampRaw.toString('hex')})`);

  const referenceTimestampInteger = (referenceTimestampRaw[0] << 24 | referenceTimestampRaw[1] << 16 | referenceTimestampRaw[2] << 8 | referenceTimestampRaw[3]) >>> 0;
  const referenceTimestampFraction = (referenceTimestampRaw[4] << 24 | referenceTimestampRaw[5] << 16 | referenceTimestampRaw[6] << 8 | referenceTimestampRaw[7]) >>> 0;
  const referenceTimestamp = referenceTimestampInteger + referenceTimestampFraction / 0x100000000;
  log.info(`Reference Timestamp: ${referenceTimestamp} , ${referenceTimestampInteger} ${referenceTimestampFraction}`);
  const ntpSeconds: bigint = BigInt(Math.floor(referenceTimestampInteger));
  const NTP_EPOCH_OFFSET = 2208988800n;
  const unixSeconds: bigint = ntpSeconds - NTP_EPOCH_OFFSET;

  const unixMs: number = Number(unixSeconds) * 1000 + (referenceTimestampFraction % 1) * 1000;
  const date: Date = new Date(unixMs);
  log.info(`Reference Timestamp: ${date.toISOString()}`);
}

main().catch((err) => {
  log.error(`{main} [ERROR] ${err.message}`);
  process.exit(1);
});
