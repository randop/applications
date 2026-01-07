package mail.utility.sanitizer;

import java.io.*;
import java.nio.file.*;
import java.util.Properties;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.apache.commons.codec.net.QuotedPrintableCodec;
import org.jsoup.Jsoup;
import org.jsoup.nodes.Document;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import jakarta.mail.*;
import jakarta.mail.internet.MimeMessage;
import jakarta.mail.internet.MimeMultipart;

public class EmailCleaner {

    private static final Logger log = LoggerFactory.getLogger(EmailCleaner.class);

    private final Path sourceDirectory;
    private final Path targetDirectory;

    public EmailCleaner(String sourceDirPath, String targetDirPath) {
        this.sourceDirectory = Paths.get(sourceDirPath);
        this.targetDirectory = Paths.get(targetDirPath);

        try {
            Files.createDirectories(targetDirectory);
            log.info("Target directory ensured: {}", targetDirectory.toAbsolutePath());
        } catch (IOException e) {
            log.error("Failed to create target directory: {}", targetDirectory, e);
            throw new RuntimeException("Cannot create target directory: " + targetDirectory, e);
        }
    }

    /**
     * Processes all .eml files in the source directory and saves cleaned versions
     * to the target directory.
     */
    public void process() throws IOException {
        if (!Files.isDirectory(sourceDirectory)) {
            log.error("Source path is not a directory: {}", sourceDirectory);
            throw new IllegalArgumentException("Source path is not a directory: " + sourceDirectory);
        }

        log.info("Starting processing of all files in: {}", sourceDirectory.toAbsolutePath());

        try (DirectoryStream<Path> stream = Files.newDirectoryStream(sourceDirectory)) {
            int processedCount = 0;
            for (Path inputPath : stream) {
                if (!Files.isRegularFile(inputPath)) {
                    if (log.isDebugEnabled()) {
                        log.debug("Skipping non-regular file: {}", inputPath.getFileName());
                    }
                    continue;
                }

                String fileName = inputPath.getFileName().toString();
                log.info("Processing file {}: {}", ++processedCount, fileName);

                try {
                    sanitize(inputPath);
                    if (log.isDebugEnabled()) {
                        log.debug("Successfully cleaned: {}", fileName);
                    }
                } catch (Exception e) {
                    log.error("Failed to process file: {}", fileName, e);
                }
            }

            if (processedCount == 0) {
                log.warn("No files found in directory: {}", sourceDirectory);
            }
        }

        log.info("Processing complete. Cleaned emails saved to: {}", targetDirectory.toAbsolutePath());
    }

    /**
     * Sanitizes a single .eml file: removes HTML, tracking, converts to plain text,
     * preserves headers.
     */
    private void sanitize(Path inputPath) throws Exception {
        Properties props = new Properties();
        props.put("mail.mime.messageid", "false");
        Session session = Session.getDefaultInstance(props);

        try (InputStream is = Files.newInputStream(inputPath)) {
            MimeMessage original = new EmailMimeMessage(session, is);

            String subject = original.getSubject();
            if (log.isDebugEnabled()) {
                log.debug("Loaded message - Subject: {}", subject != null ? subject : "(no subject)");
            }

            String plainTextBody = extract(original);

            MimeMessage cleanMessage = new EmailMimeMessage(session);
            headers(original, cleanMessage);
            cleanMessage.setText(plainTextBody, "utf-8");
            // Explicitly preserve original Message-ID
            String[] originalMsgId = original.getHeader("Message-ID");
            if (originalMsgId != null && originalMsgId.length > 0) {
                if (log.isDebugEnabled()) {
                    log.debug("message-id: {}", originalMsgId[0]);
                }
                cleanMessage.setHeader("Message-ID", originalMsgId[0]);
            } else {
                cleanMessage.setHeader("Message-ID", UUID.randomUUID().toString());
            }
            cleanMessage.saveChanges();

            String originalFileName = inputPath.getFileName().toString();
            String uuid = resolveUUID(originalFileName);

            Path outputPath = targetDirectory.resolve(uuid + ".eml");

            try (OutputStream os = Files.newOutputStream(outputPath)) {
                cleanMessage.writeTo(os);
            }

            log.info("Saved cleaned version: {} → {}", originalFileName, outputPath.getFileName());
        }
    }

    /**
     * Extracts readable plain text from the email message.
     */
    private String extract(MimeMessage message) throws Exception {
        Object content = message.getContent();

        if (content instanceof String text) {
            if (message.isMimeType("text/plain")) {
                if (log.isDebugEnabled()) {
                    log.debug("Found direct text/plain content");
                }
                return text;
            } else if (message.isMimeType("text/html")) {
                if (log.isDebugEnabled()) {
                    log.debug("Converting text/html to plain text");
                }
                return plain(message, text);
            }
        } else if (content instanceof Multipart mp) {
            if (log.isDebugEnabled()) {
                log.debug("Processing multipart message with {} parts", mp.getCount());
            }

            for (int i = 0; i < mp.getCount(); i++) {
                BodyPart part = mp.getBodyPart(i);
                Object partContent = part.getContent();
                if (log.isDebugEnabled()) {
                    log.info(
                            "Multi-part email type: {}, {}",
                            part.getContentType(),
                            partContent.getClass().descriptorString());
                }
                if (part.isMimeType("text/plain") && partContent instanceof String text) {
                    return text;
                } else if (part.isMimeType("text/html") && partContent instanceof String html) {
                    return plain(message, html);
                } else if (part.isMimeType("multipart/related") && partContent instanceof MimeMultipart) {
                    return multipartRecurse(message, 0);
                }
            }
        }

        log.warn("No readable text content found in this message");
        return "(empty)";
    }

    /**
     * Copies important headers from original to cleaned message.
     */
    private void headers(MimeMessage from, MimeMessage to) throws MessagingException {
        copyHeader(from, to, "From");
        copyHeader(from, to, "To");
        copyHeader(from, to, "Cc");
        copyHeader(from, to, "Bcc");
        copyHeader(from, to, "Subject");
        copyHeader(from, to, "Date");
        copyHeader(from, to, "Message-ID");
        copyHeader(from, to, "In-Reply-To");
        copyHeader(from, to, "Reply-To");
        copyHeader(from, to, "References");
    }

    private void copyHeader(MimeMessage from, MimeMessage to, String headerName) throws MessagingException {
        String[] values = from.getHeader(headerName);
        if (values != null && values.length > 0) {
            to.setHeader(headerName, values[0]);
        }
    }

    private String resolveUUID(String fileName) {
        Pattern pattern = Pattern.compile("^([0-9a-fA-F-]{30,})(?::.*)?$");
        Matcher matcher = pattern.matcher(fileName);

        if (matcher.matches()) {
            return matcher.group(1);
        }

        // Fallback: generate a new random UUID
        String generated = UUID.randomUUID().toString();
        log.warn("Could not resolve valid UUID from filename: {}. Generated new UUID: {}", fileName, generated);
        return generated;
    }

    /**
     * Converts HTML to plain text while preserving hyperlinks in a readable format.
     */
    private String plain(MimeMessage message, String html) {
        if (html == null || html.trim().isEmpty()) {
            return "";
        }

        try {
            String[] encoding = message.getHeader("Content-Transfer-Encoding");
            if (encoding.length > 0 && !encoding[0].trim().isBlank()) {
                String contentEncoding = encoding[0].trim().toLowerCase();
                if (contentEncoding.equalsIgnoreCase("quoted-printable")) {
                    QuotedPrintableCodec codec = new QuotedPrintableCodec();
                    byte[] decodedBytes = codec.decode(html.getBytes("US-ASCII"));
                    String decodedHtml = new String(decodedBytes, "UTF-8");
                    Document doc = Jsoup.parse(decodedHtml);
                    return doc.wholeText();
                }
            }
        } catch (NullPointerException ex) {
            if (!ex.getMessage().contains("Cannot read the array length because")) {
                log.error(ex.getMessage());
            }
        } catch (Exception ex) {
            log.error(ex.getMessage());
        }

        Document doc = Jsoup.parse(html);
        return doc.wholeText();
    }

    /**
     * Recursively processes MIME parts to extract the best available plain text.
     * Depth limited to 5 to prevent infinite recursion on malformed emails.
     *
     * @param part  The current MIME part (Message or BodyPart)
     * @param depth Current recursion depth
     * @return Plain text content, or empty string if none found at this level
     */
    private String multipartRecurse(Part part, int depth) throws Exception {
        if (depth > 5) {
            return "";
        }

        Object content = part.getContent();

        if (content instanceof Multipart) {
            Multipart mp = (Multipart) content;
            for (int i = 0; i < mp.getCount(); i++) {
                BodyPart bodyPart = mp.getBodyPart(i);

                String disposition = bodyPart.getDisposition();
                if (disposition != null
                        && (disposition.equalsIgnoreCase(Part.ATTACHMENT)
                                || (disposition.equalsIgnoreCase(Part.INLINE) && bodyPart.getFileName() != null))) {
                    continue;
                }

                String result = multipartRecurse(bodyPart, depth + 1);
                if (result != null && !result.isEmpty()) {
                    return result;
                }
            }
            return "";
        }

        if (content instanceof String) {
            String text = (String) content;
            String contentType = part.getContentType();

            if (contentType != null) {
                String lowerType = contentType.toLowerCase();

                if (lowerType.contains("text/html")) {
                    return Jsoup.parse(text).wholeText();
                } else if (lowerType.contains("text/plain")) {
                    return text.trim();
                }
            }

            return text.trim();
        }

        return "";
    }
}
