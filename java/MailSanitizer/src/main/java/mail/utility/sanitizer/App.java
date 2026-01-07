package mail.utility.sanitizer;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.Properties;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class App {
    private static final Logger log = LoggerFactory.getLogger(App.class);
    private static final String CONFIG_FILE = "/etc/mail/sanitizer.properties";
    private static final String PROP_SOURCE_DIR = "source.directory";
    private static final String PROP_TARGET_DIR = "target.directory";

    public static void main(String[] args) {
        log.info("MailSanitizer initializing...");
        Properties config = loadConfiguration();
        String sourceDirectory = config.getProperty(PROP_SOURCE_DIR);
        String targetDirectory = config.getProperty(PROP_TARGET_DIR);
        if (sourceDirectory != null
                && !sourceDirectory.trim().isBlank()
                && targetDirectory != null
                && !targetDirectory.trim().isBlank()) {
            try {
                EmailCleaner cleaner = new EmailCleaner(sourceDirectory, targetDirectory);
                cleaner.process();
            } catch (Exception ex) {
                log.error(ex.getMessage());
            }
        } else {
            log.error("Invalid source or target directory configuration at {}", CONFIG_FILE);
        }
    }

    private static Properties loadConfiguration() {
        Properties props = new Properties();
        if (loadPropertiesFromFile(props, CONFIG_FILE)) {
            log.info("Loaded configuration at {}", CONFIG_FILE);
        }
        return props;
    }

    private static boolean loadPropertiesFromFile(Properties props, String path) {
        File file = new File(path);
        if (file.exists() && file.isFile() && file.canRead()) {
            try (FileInputStream fis = new FileInputStream(file)) {
                props.load(fis);
                return true;
            } catch (IOException e) {
                log.warn("Failed to read configuration file: {}", path, e);
            }
        }
        return false;
    }
}
