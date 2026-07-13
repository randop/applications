package com.kanban.local.crypto;

import org.springframework.stereotype.Service;

import javax.crypto.Cipher;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.PBEKeySpec;
import javax.crypto.spec.SecretKeySpec;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.Base64;

/**
 * Per-document AES-256-GCM encryption. Each card owns an independent raw
 * key + salt; the actual AES key is derived via PBKDF2-HMAC-SHA256 from
 * both, so neither value alone decrypts anything. Key and salt are
 * generated and consumed here only - callers never persist them remotely.
 */
@Service
public class CryptoService {

    private static final int GCM_IV_LENGTH = 12;
    private static final int GCM_TAG_LENGTH_BITS = 128;
    private static final int PBKDF2_ITERATIONS = 120_000;
    private static final int AES_KEY_LENGTH_BITS = 256;

    private final SecureRandom secureRandom = new SecureRandom();

    public String generateRawKey() {
        byte[] raw = new byte[32];
        secureRandom.nextBytes(raw);
        return Base64.getEncoder().encodeToString(raw);
    }

    public String generateSalt() {
        byte[] salt = new byte[16];
        secureRandom.nextBytes(salt);
        return Base64.getEncoder().encodeToString(salt);
    }

    public EncryptedField encrypt(String plainText, String base64RawKey, String base64Salt) {
        try {
            SecretKeySpec key = deriveKey(base64RawKey, base64Salt);
            byte[] iv = new byte[GCM_IV_LENGTH];
            secureRandom.nextBytes(iv);

            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, key, new GCMParameterSpec(GCM_TAG_LENGTH_BITS, iv));
            byte[] cipherBytes = cipher.doFinal(plainText.getBytes(StandardCharsets.UTF_8));

            return new EncryptedField(
                    Base64.getEncoder().encodeToString(cipherBytes),
                    Base64.getEncoder().encodeToString(iv)
            );
        } catch (Exception e) {
            throw new IllegalStateException("Encryption failed", e);
        }
    }

    public String decrypt(EncryptedField field, String base64RawKey, String base64Salt) {
        if (field == null || field.cipherText() == null) {
            return null;
        }
        try {
            SecretKeySpec key = deriveKey(base64RawKey, base64Salt);
            byte[] iv = Base64.getDecoder().decode(field.iv());
            byte[] cipherBytes = Base64.getDecoder().decode(field.cipherText());

            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, key, new GCMParameterSpec(GCM_TAG_LENGTH_BITS, iv));
            byte[] plainBytes = cipher.doFinal(cipherBytes);

            return new String(plainBytes, StandardCharsets.UTF_8);
        } catch (Exception e) {
            throw new IllegalStateException("Decryption failed", e);
        }
    }

    private SecretKeySpec deriveKey(String base64RawKey, String base64Salt) throws Exception {
        char[] password = base64RawKey.toCharArray();
        byte[] salt = Base64.getDecoder().decode(base64Salt);

        SecretKeyFactory factory = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256");
        PBEKeySpec spec = new PBEKeySpec(password, salt, PBKDF2_ITERATIONS, AES_KEY_LENGTH_BITS);
        byte[] keyBytes = factory.generateSecret(spec).getEncoded();
        return new SecretKeySpec(keyBytes, "AES");
    }
}
