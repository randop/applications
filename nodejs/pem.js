#!/usr/bin/env node
/*****************************************************************************
pem.js - Extracts information on PEM certificate file and produce json output
Requires: Node.js version 20 or higher

Copyright © 2010 — 2026 Randolph Ledesma

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*****************************************************************************/

const fs = require("fs");
const path = require("path");
const crypto = require("crypto");

function parseDistinguishedName(dnString) {
  const result = {};
  let normalized = dnString
    .replace(/\n/g, ", ")
    .replace(/\s*,\s*/g, ",")
    .trim();

  const parts = normalized.split(",").map((p) => p.trim()).filter((p) => p);

  for (const part of parts) {
    const match = part.match(/^([^=]+?)\s*=\s*(.+)$/);
    if (match) {
      const key = match[1].trim();
      let value = match[2].trim();
      if (value.startsWith('"') && value.endsWith('"')) {
        value = value.slice(1, -1);
      }
      result[key] = value;
    }
  }
  return result;
}

/**
 * Converts OpenSSL date to unix timestamp (seconds)
 */
function dateToEpoch(dateStr) {
  if (!dateStr) return null;
  const ms = Date.parse(dateStr);
  return Number.isFinite(ms) ? Math.floor(ms / 1000) : null;
}

/**
 * Primary PEM certificate parser
 */
function parseCertificatesFromPem(content) {
  const certs = [];

  const controller = new AbortController();
  const { signal } = controller;

  // Start 30-second timeout
  const timeoutId = setTimeout(() => controller.abort(), 30_000);

  try {
    let start = 0;
    let iteration = 0;

    while (true) {
      // === TIMEOUT CHECK ===
      if (signal.aborted) {
        throw new Error(
          `Certificate parsing timed out after 30 seconds (processed ${iteration} blocks)`,
        );
      }

      const beginIndex = content.indexOf("-----BEGIN CERTIFICATE-----", start);
      if (beginIndex === -1) break;

      const endIndex = content.indexOf("-----END CERTIFICATE-----", beginIndex);
      if (endIndex === -1) break;

      const end =
        content.indexOf("\n", endIndex + "-----END CERTIFICATE-----".length) +
        1;
      if (end <= endIndex) break;

      const pemBlock = content.slice(beginIndex, end).trim();

      try {
        const x509 = new crypto.X509Certificate(pemBlock);

        const parsedSubject = parseDistinguishedName(x509.subject);
        const parsedIssuer = parseDistinguishedName(x509.issuer);

        const notBeforeEpoch = dateToEpoch(x509.validFrom);
        const notAfterEpoch = dateToEpoch(x509.validTo);

        const nowEpoch = Math.floor(Date.now() / 1000);
        const daysLeft = notAfterEpoch
          ? Math.floor((notAfterEpoch - nowEpoch) / 86400)
          : null;

        certs.push({
          subjectRaw: x509.subject,
          subject: parsedSubject,
          issuerRaw: x509.issuer,
          issuer: parsedIssuer,
          serial: x509.serialNumber,
          notBefore: x509.validFrom,
          notAfter: x509.validTo,
          notBeforeEpoch,
          notAfterEpoch,
          daysLeft,
          fingerprint: x509.fingerprint256 || x509.fingerprint,
        });
      } catch (err) {
        console.error("Failed to parse one certificate block:", err.message);
      }

      start = end;
      iteration++;
    }

    return certs.length > 0
      ? certs
      : [{ error: "No valid certificates found in PEM" }];
  } catch (err) {
    return [{ error: err.message || "Certificate parsing failed" }];
  } finally {
    clearTimeout(timeoutId);
  }
}

function main() {
  const args = process.argv.slice(2);
  if (args.length === 0) {
    console.error("Usage: node pem.js <path-to-pem-file>");
    process.exit(1);
  }

  const filePath = path.resolve(args[0]);

  let content;
  try {
    content = fs.readFileSync(filePath, "utf8");
  } catch (err) {
    console.error(`Cannot read file:`, err.message);
    process.exit(1);
  }

  const result = parseCertificatesFromPem(content);
  console.log(JSON.stringify(result, null, 2));
}

if (require.main === module) {
  main();
}
