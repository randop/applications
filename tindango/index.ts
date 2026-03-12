import {
  createClient,
  id,
  CreateAccountError,
  CreateTransferError,
  AccountFlags,
  type Account,
  type Transfer,
} from "tigerbeetle-node";
import { strict as assert } from "node:assert";
import process from "node:process";

async function main() {
  const client = createClient({
    cluster_id: 0n,
    replica_addresses: [process.env.TB_ADDRESS || "3000"],
  });

  try {
    const accounts: Account[] = [
      {
        id: id(),
        debits_pending: 0n,
        debits_posted: 0n,
        credits_pending: 0n,
        credits_posted: 0n,
        user_data_128: 0n,
        user_data_64: 0n,
        user_data_32: 0,
        reserved: 0,
        ledger: 1,
        code: 1,
        flags: AccountFlags.debits_must_not_exceed_credits | AccountFlags.history,
        timestamp: 0n,
      } satisfies Account,
      {
        id: id(),
        debits_pending: 0n,
        debits_posted: 0n,
        credits_pending: 0n,
        credits_posted: 0n,
        user_data_128: 0n,
        user_data_64: 0n,
        user_data_32: 0,
        reserved: 0,
        ledger: 1,
        code: 1,
        flags: AccountFlags.debits_must_not_exceed_credits | AccountFlags.history,
        timestamp: 0n,
      } satisfies Account,
    ];

    console.log("Accounts:", accounts);
    const accountErrors = await client.createAccounts(accounts);

    for (const error of accountErrors) {
      console.error(
        `Batch account at index ${error.index} failed: ${CreateAccountError[error.result]}`,
      );
    }

    assert.strictEqual(accountErrors.length, 0, "Account creation failed");

    console.log("ok");
  } catch (err) {
    console.error("Error in main:", err);
    process.exit(1);
  } finally {
    client.destroy();
  }
}

main()
  .then(() => process.exit(0))
  .catch((err) => {
    console.error("Unhandled error:", err);
    process.exit(1);
  });
