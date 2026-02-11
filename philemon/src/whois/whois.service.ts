import { Injectable } from "@nestjs/common";
import { exec } from "child_process";
import { promisify } from "util";

const execAsync = promisify(exec);

@Injectable()
export class WhoisService {
  async lookup(domain: string): Promise<string> {
    try {
      const { stdout } = await execAsync(`whois ${domain}`);
      return stdout;
    } catch (error) {
      throw new Error(`Whois lookup failed: ${error.message}`);
    }
  }
}
