import { describe, it, expect } from 'bun:test';
import net from 'net';

const PORT = 25999;

describe('smtp_server_client_test', () => {
    it('should complete full SMTP transaction', async () => {
        const client = net.createConnection({ port: PORT }, () => {
            console.log(`Connected to smtp server on host on port ${PORT}`);
        });

        const responses: string[] = [];
        client.on('data', (data) => {
            const msg = data.toString();
            responses.push(msg);
            console.log('S:', msg.trim());
        });

        // Simulate client flow
        await new Promise(r => setTimeout(r, 100));
        client.write('EHLO test.local\r\n');

        await new Promise(r => setTimeout(r, 100));
        client.write('MAIL FROM:<sender@example.com>\r\n');

        await new Promise(r => setTimeout(r, 100));
        client.write('RCPT TO:<recipient@example.com>\r\n');

        await new Promise(r => setTimeout(r, 100));
        client.write('DATA\r\n');

        await new Promise(r => setTimeout(r, 100));
        client.write('Subject: Test from Bun\r\n\r\n');
        client.write('Hello from Bun test!\r\n');
        client.write('.\r\n');

        await new Promise(r => setTimeout(r, 100));
        client.write('QUIT\r\n');

        await new Promise(r => setTimeout(r, 300));
        client.end();

        // Basic assertions
        expect(responses.some(r => r.includes('220'))).toBe(true);
        expect(responses.some(r => r.includes('250 OK'))).toBe(true);
        expect(responses.some(r => r.includes('354'))).toBe(true);
        expect(responses.some(r => r.includes('221'))).toBe(true);
    });
});
