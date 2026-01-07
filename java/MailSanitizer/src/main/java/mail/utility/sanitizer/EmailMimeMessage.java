package mail.utility.sanitizer;

import java.io.InputStream;

import jakarta.mail.MessagingException;
import jakarta.mail.Session;
import jakarta.mail.internet.MimeMessage;

/**
 * Custom MimeMessage that preserves the original Message-ID
 * by preventing auto-generation in updateMessageID().
 */
public class EmailMimeMessage extends MimeMessage {

    public EmailMimeMessage(Session session, InputStream is) throws MessagingException {
        super(session, is);
    }

    public EmailMimeMessage(Session session) {
        super(session);
    }

    @Override
    protected void updateMessageID() throws MessagingException {
        // Do NOTHING — prevents Jakarta Mail from generating a new ID like
        // Original Message-ID (if present) remains untouched
    }
}
