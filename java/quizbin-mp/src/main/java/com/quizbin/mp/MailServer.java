package com.quizbin.mp;

import java.util.Properties;
import java.util.logging.Logger;

import com.icegreen.greenmail.user.GreenMailUser;
import com.icegreen.greenmail.user.UserManager;
import com.icegreen.greenmail.util.GreenMail;
import com.icegreen.greenmail.util.ServerSetup;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import jakarta.enterprise.context.ApplicationScoped;
import jakarta.enterprise.context.Initialized;
import jakarta.enterprise.event.Observes;

@ApplicationScoped
public class MailServer {
    private static final Logger log = Logger.getLogger(MailServer.class.getName());
    private GreenMail greenMail;

    // This forces eager instantiation + runs @PostConstruct at startup
    protected void onApplicationInitialized(@Observes @Initialized(ApplicationScoped.class) Object event) {
        log.info("MailServer initialized");
    }

    @PostConstruct
    void start() {
        Properties props = new Properties();
        props.setProperty("greenmail.auth.disabled", "false");
        props.setProperty("greenmail.auth.imap.plain.disabled", "true");
        props.setProperty("greenmail.auth.imap.login.disabled", "true");

        ServerSetup imapSetup = new ServerSetup(3143, "0.0.0.0", ServerSetup.PROTOCOL_IMAP);
        greenMail = new GreenMail(imapSetup);

        // Manually add to UserManager with a dummy password (not used for XOAUTH2)
        // XOAUTH2 auth validates the email address in the token, not the password.
        UserManager userManager = greenMail.getManagers().getUserManager();
        try {
            GreenMailUser user = userManager.createUser("testuser@example.com", "testuser", "dummyPassword");
            log.info("Account Authentication: " + user.getEmail());
        } catch (Exception ex) {
            ex.printStackTrace();
        }

        greenMail.start();

        log.info("IMAP started on port " + greenMail.getImap().getPort());
    }

    @PreDestroy
    void stop() {
        if (greenMail != null) {
            greenMail.stop();
        }
    }
}
