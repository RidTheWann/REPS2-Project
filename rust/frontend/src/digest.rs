// SPDX-License-Identifier: GPL-3.0-or-later
//! Content hashing shared by the library importer and the build manifest.
//!
//! The frontend does not depend on external crates, so this module carries a
//! small, self-contained SHA-256 implementation. Digests are recorded as
//! lowercase hexadecimal so manifests stay stable across runs and platforms.

use std::io::Read;

/// Renders a 32-byte digest as 64 lowercase hexadecimal characters.
#[must_use]
pub fn hex_digest(digest: &[u8; 32]) -> String {
    let mut output = String::with_capacity(64);
    for byte in digest {
        output.push_str(&format!("{byte:02x}"));
    }
    output
}

/// Hashes a complete byte slice.
#[must_use]
pub fn sha256_bytes(bytes: &[u8]) -> [u8; 32] {
    let mut hasher = Sha256::new();
    hasher.update(bytes);
    hasher.finalize()
}

/// Streams a reader through SHA-256 without buffering the whole input.
///
/// # Errors
///
/// Returns a message when the reader fails.
pub fn sha256_reader<R: Read>(reader: &mut R) -> Result<[u8; 32], String> {
    let mut hasher = Sha256::new();
    let mut buffer = [0u8; 64 * 1024];
    loop {
        let count = reader
            .read(&mut buffer)
            .map_err(|error| format!("read failed while hashing input: {error}"))?;
        if count == 0 {
            break;
        }
        hasher.update(&buffer[..count]);
    }
    Ok(hasher.finalize())
}

/// Incremental SHA-256 state.
pub struct Sha256 {
    state: [u32; 8],
    buffer: [u8; 64],
    buffered: usize,
    total_bytes: u64,
}

impl Sha256 {
    const INITIAL_STATE: [u32; 8] = [
        0x6A09_E667,
        0xBB67_AE85,
        0x3C6E_F372,
        0xA54F_F53A,
        0x510E_527F,
        0x9B05_688C,
        0x1F83_D9AB,
        0x5BE0_CD19,
    ];

    const ROUND_CONSTANTS: [u32; 64] = [
        0x428A_2F98,
        0x7137_4491,
        0xB5C0_FBCF,
        0xE9B5_DBA5,
        0x3956_C25B,
        0x59F1_11F1,
        0x923F_82A4,
        0xAB1C_5ED5,
        0xD807_AA98,
        0x1283_5B01,
        0x2431_85BE,
        0x550C_7DC3,
        0x72BE_5D74,
        0x80DE_B1FE,
        0x9BDC_06A7,
        0xC19B_F174,
        0xE49B_69C1,
        0xEFBE_4786,
        0x0FC1_9DC6,
        0x240C_A1CC,
        0x2DE9_2C6F,
        0x4A74_84AA,
        0x5CB0_A9DC,
        0x76F9_88DA,
        0x983E_5152,
        0xA831_C66D,
        0xB003_27C8,
        0xBF59_7FC7,
        0xC6E0_0BF3,
        0xD5A7_9147,
        0x06CA_6351,
        0x1429_2967,
        0x27B7_0A85,
        0x2E1B_2138,
        0x4D2C_6DFC,
        0x5338_0D13,
        0x650A_7354,
        0x766A_0ABB,
        0x81C2_C92E,
        0x9272_2C85,
        0xA2BF_E8A1,
        0xA81A_664B,
        0xC24B_8B70,
        0xC76C_51A3,
        0xD192_E819,
        0xD699_0624,
        0xF40E_3585,
        0x106A_A070,
        0x19A4_C116,
        0x1E37_6C08,
        0x2748_774C,
        0x34B0_BCB5,
        0x391C_0CB3,
        0x4ED8_AA4A,
        0x5B9C_CA4F,
        0x682E_6FF3,
        0x748F_82EE,
        0x78A5_636F,
        0x84C8_7814,
        0x8CC7_0208,
        0x90BE_FFFA,
        0xA450_6CEB,
        0xBEF9_A3F7,
        0xC671_78F2,
    ];

    /// Creates an empty hasher.
    #[must_use]
    pub fn new() -> Self {
        Self {
            state: Self::INITIAL_STATE,
            buffer: [0; 64],
            buffered: 0,
            total_bytes: 0,
        }
    }

    /// Appends bytes to the message.
    pub fn update(&mut self, mut input: &[u8]) {
        self.total_bytes = self.total_bytes.wrapping_add(input.len() as u64);

        if self.buffered != 0 {
            let take = (64 - self.buffered).min(input.len());
            self.buffer[self.buffered..self.buffered + take].copy_from_slice(&input[..take]);
            self.buffered += take;
            input = &input[take..];
            if self.buffered == 64 {
                let block = self.buffer;
                self.compress(&block);
                self.buffered = 0;
            }
        }

        while input.len() >= 64 {
            let block: &[u8; 64] = input[..64].try_into().expect("64-byte SHA-256 block");
            self.compress(block);
            input = &input[64..];
        }

        if !input.is_empty() {
            self.buffer[..input.len()].copy_from_slice(input);
            self.buffered = input.len();
        }
    }

    /// Finishes the message and returns its digest.
    #[must_use]
    pub fn finalize(mut self) -> [u8; 32] {
        let bit_length = self.total_bytes.wrapping_mul(8);
        self.buffer[self.buffered] = 0x80;
        self.buffered += 1;

        if self.buffered > 56 {
            self.buffer[self.buffered..].fill(0);
            let block = self.buffer;
            self.compress(&block);
            self.buffer = [0; 64];
            self.buffered = 0;
        } else {
            self.buffer[self.buffered..56].fill(0);
        }

        self.buffer[56..64].copy_from_slice(&bit_length.to_be_bytes());
        let block = self.buffer;
        self.compress(&block);

        let mut digest = [0u8; 32];
        for (index, word) in self.state.iter().enumerate() {
            digest[index * 4..index * 4 + 4].copy_from_slice(&word.to_be_bytes());
        }
        digest
    }

    fn compress(&mut self, block: &[u8; 64]) {
        let mut schedule = [0u32; 64];
        for (index, word) in schedule.iter_mut().take(16).enumerate() {
            let offset = index * 4;
            *word = u32::from_be_bytes([
                block[offset],
                block[offset + 1],
                block[offset + 2],
                block[offset + 3],
            ]);
        }

        for index in 16..64 {
            let x = schedule[index - 15];
            let y = schedule[index - 2];
            let sigma0 = x.rotate_right(7) ^ x.rotate_right(18) ^ (x >> 3);
            let sigma1 = y.rotate_right(17) ^ y.rotate_right(19) ^ (y >> 10);
            schedule[index] = schedule[index - 16]
                .wrapping_add(sigma0)
                .wrapping_add(schedule[index - 7])
                .wrapping_add(sigma1);
        }

        let [mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut h] = self.state;
        for (index, schedule_word) in schedule.iter().copied().enumerate() {
            let sum1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let choice = (e & f) ^ ((!e) & g);
            let temp1 = h
                .wrapping_add(sum1)
                .wrapping_add(choice)
                .wrapping_add(Self::ROUND_CONSTANTS[index])
                .wrapping_add(schedule_word);
            let sum0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let majority = (a & b) ^ (a & c) ^ (b & c);
            let temp2 = sum0.wrapping_add(majority);

            h = g;
            g = f;
            f = e;
            e = d.wrapping_add(temp1);
            d = c;
            c = b;
            b = a;
            a = temp1.wrapping_add(temp2);
        }

        self.state[0] = self.state[0].wrapping_add(a);
        self.state[1] = self.state[1].wrapping_add(b);
        self.state[2] = self.state[2].wrapping_add(c);
        self.state[3] = self.state[3].wrapping_add(d);
        self.state[4] = self.state[4].wrapping_add(e);
        self.state[5] = self.state[5].wrapping_add(f);
        self.state[6] = self.state[6].wrapping_add(g);
        self.state[7] = self.state[7].wrapping_add(h);
    }
}

impl Default for Sha256 {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sha256_matches_standard_known_answer_vectors() {
        assert_eq!(
            hex_digest(&sha256_bytes(b"")),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        );
        assert_eq!(
            hex_digest(&sha256_bytes(b"abc")),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        assert_eq!(
            hex_digest(&sha256_bytes(&vec![b'a'; 1_000_000])),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"
        );
    }

    #[test]
    fn sha256_is_independent_of_update_chunk_boundaries() {
        let payload = (0..=255).cycle().take(8193).collect::<Vec<_>>();
        let expected = sha256_bytes(&payload);
        let mut hasher = Sha256::new();
        for chunk in payload.chunks(37) {
            hasher.update(chunk);
        }
        assert_eq!(hasher.finalize(), expected);
    }

    #[test]
    fn sha256_reader_matches_in_memory_hashing() {
        let payload = (0..=255).cycle().take(70_001).collect::<Vec<_>>();
        let mut reader = payload.as_slice();
        assert_eq!(
            sha256_reader(&mut reader).expect("hashing a slice succeeds"),
            sha256_bytes(&payload)
        );
    }

    /// SHA-256 pads to a 64-byte block with a 9-byte minimum trailer, so the
    /// interesting lengths are those straddling 55/56 and 63/64 bytes. The
    /// expected digests were produced independently with Python's `hashlib`.
    #[test]
    fn sha256_matches_independent_vectors_at_padding_boundaries() {
        let vectors: [(usize, &str); 8] = [
            (
                55,
                "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318",
            ),
            (
                56,
                "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a",
            ),
            (
                63,
                "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34",
            ),
            (
                64,
                "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
            ),
            (
                65,
                "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0",
            ),
            (
                119,
                "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb",
            ),
            (
                120,
                "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c",
            ),
            (
                128,
                "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e",
            ),
        ];
        for (length, expected) in vectors {
            assert_eq!(
                hex_digest(&sha256_bytes(&vec![b'a'; length])),
                expected,
                "length {length}"
            );
        }
    }

    #[test]
    fn sha256_of_the_two_block_nist_message_matches() {
        assert_eq!(
            hex_digest(&sha256_bytes(
                b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
            )),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
        );
    }
}
