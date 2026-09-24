// qnt - Quant phone wallet for Termux (also runs on any Linux/Windows terminal).
// Light (SPV) client: keeps headers only, verifies coins with Merkle proofs, signs locally.
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

#include <qrcodegen.hpp>

#include "consensus/limits.h"
#include "light/light_client.h"
#include "util/log.h"
#include "wallet/mnemonic.h"

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

using namespace quant;

// ---------------------------------------------------------------- terminal helpers
static const char* C_RESET = "\033[0m";
static const char* C_DIM = "\033[2m";
static const char* C_BOLD = "\033[1m";
static const char* C_GREEN = "\033[32m";
static const char* C_RED = "\033[31m";
static const char* C_YEL = "\033[33m";
static const char* C_PURP = "\033[38;5;141m";

static std::string ask(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string s;
    std::getline(std::cin, s);
    return s;
}

static std::string ask_hidden(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string s;
#ifdef _WIN32
    int c;
    while ((c = _getch()) != '\r' && c != '\n' && c != EOF) { if (c == '\b') { if (!s.empty()) s.pop_back(); } else s += char(c); }
    std::cout << "\n";
#else
    termios old{}, noecho{};
    bool tty = tcgetattr(STDIN_FILENO, &old) == 0;
    if (tty) { noecho = old; noecho.c_lflag &= ~ECHO; tcsetattr(STDIN_FILENO, TCSANOW, &noecho); }
    std::getline(std::cin, s);
    if (tty) { tcsetattr(STDIN_FILENO, TCSANOW, &old); std::cout << "\n"; }
#endif
    return s;
}

static void print_qr(const std::string& text) {
    using qrcodegen::QrCode;
    std::string up = text;
    for (auto& c : up) c = char(toupper((unsigned char)c));
    QrCode qr = QrCode::encodeText(up.c_str(), QrCode::Ecc::LOW);
    int n = qr.getSize(), q = 2;
    // Two QR rows per text line using half-block characters; light background for scanners.
    for (int y = -q; y < n + q; y += 2) {
        std::string line = "\033[47m\033[30m";
        for (int x = -q; x < n + q; x++) {
            bool top = qr.getModule(x, y), bot = qr.getModule(x, y + 1);
            line += top && bot ? "█" : top ? "▀" : bot ? "▄" : " ";
        }
        std::cout << line << C_RESET << "\n";
    }
}

static std::string signed_amount(int64_t d) {
    return (d < 0 ? "-" : "+") + format_amount(Amount(d < 0 ? -d : d));
}

// ---------------------------------------------------------------- app
struct App {
    Network net = Network::Test;
    std::string dir;
    std::vector<std::string> addnodes;
    std::unique_ptr<Wallet> wallet;
    std::unique_ptr<LightClient> lc;
    const ChainParams* p = nullptr;

    std::string wallet_path() const { return dir + "/wallet.qwl"; }

    bool open_wallet() {
        if (wallet) return true;
        if (!Wallet::exists(wallet_path())) {
            std::cout << C_YEL << "No wallet yet. Run: qnt new   (or: qnt restore)" << C_RESET << "\n";
            return false;
        }
        std::string err;
        wallet = Wallet::open(wallet_path(), *p, "", &err); // no password set?
        for (int tries = 0; !wallet && tries < 3; tries++)
            wallet = Wallet::open(wallet_path(), *p, ask_hidden("Wallet password: "), &err);
        if (!wallet) { std::cout << C_RED << err << C_RESET << "\n"; return false; }
        return true;
    }

    bool online() {
        if (lc && lc->peer_count()) return true;
        lc = std::make_unique<LightClient>(*p, dir);
        lc->on_status = [](const std::string& s) { std::cout << C_DIM << "  " << s << C_RESET << "\n"; };
        std::cout << C_DIM << "Finding Quant nodes (saved peers, local network, BitTorrent DHT)..." << C_RESET << "\n";
        int n = lc->connect(addnodes, 3, 25);
        if (!n) {
            std::cout << C_RED << "No Quant nodes found." << C_RESET
                      << " Is your PC/Pi node running? Try: qnt --addnode=<pc-ip>:" << p->p2p_port << " balance\n";
            return false;
        }
        std::string err;
        std::cout << C_DIM << "Connected to " << n << " node(s). Verifying block headers..." << C_RESET << "\n";
        if (!lc->sync_headers(&err)) { std::cout << C_RED << err << C_RESET << "\n"; return false; }
        return true;
    }

    bool sync_wallet() {
        if (!open_wallet() || !online()) return false;
        std::string err;
        if (!lc->refresh_wallet(*wallet, &err)) { std::cout << C_RED << err << C_RESET << "\n"; return false; }
        return true;
    }

    void create(bool restore) {
        if (Wallet::exists(wallet_path())) { std::cout << C_YEL << "A wallet already exists at " << wallet_path() << C_RESET << "\n"; return; }
        std::string words;
        if (restore) {
            words = ask("Enter your 24 seed words: ");
            if (!mnemonic_valid(words)) { std::cout << C_RED << "Those words are not valid (spelling, count or checksum)." << C_RESET << "\n"; return; }
        } else {
            words = mnemonic_generate();
            std::cout << "\n" << C_YEL << C_BOLD << "WRITE THESE 24 WORDS DOWN (on paper, in order):" << C_RESET << "\n\n";
            std::istringstream is(words);
            std::string w;
            int i = 0;
            while (is >> w) { printf("  %2d. %-12s", ++i, w.c_str()); if (i % 3 == 0) printf("\n"); }
            std::cout << "\n" << C_DIM << "They also restore this wallet in the PC client and quantd." << C_RESET << "\n\n";
            ask("Press Enter once you've written them down...");
        }
        std::string pass = ask_hidden("Optional extra passphrase (25th word, Enter for none): ");
        std::string pw = ask_hidden("Wallet file password (Enter for none): ");
        if (!pw.empty() && ask_hidden("Repeat password: ") != pw) { std::cout << C_RED << "Passwords differ." << C_RESET << "\n"; return; }
        std::cout << C_DIM << "Generating post-quantum keys (Falcon-512 + SPHINCS+)..." << C_RESET << "\n";
        std::string err;
        wallet = Wallet::create(wallet_path(), *p, words, pass, pw, 0, &err);
        if (!wallet) { std::cout << C_RED << err << C_RESET << "\n"; return; }
        std::cout << C_GREEN << "Wallet ready." << C_RESET << " Your address:\n  " << wallet->address_string(wallet->issued_keys().front()->addr) << "\n";
        if (restore) { std::cout << "Checking the network for your coins...\n"; balance(); }
    }

    void balance() {
        if (!sync_wallet()) return;
        Balance b = wallet->balance(lc->height());
        std::cout << "\n  " << C_BOLD << format_amount(b.confirmed) << " QNT" << C_RESET << "  available\n";
        if (b.unconfirmed) std::cout << "  " << format_amount(b.unconfirmed) << " QNT pending\n";
        if (b.locked) std::cout << "  " << format_amount(b.locked) << " QNT being sent\n";
        if (b.immature) std::cout << "  " << format_amount(b.immature) << " QNT mined, not yet spendable\n";
        std::cout << C_DIM << "  block " << lc->height() << ", " << lc->peer_count() << " peer(s)" << C_RESET << "\n\n";
    }

    void receive(bool fresh) {
        if (!open_wallet()) return;
        std::string a;
        if (fresh) { a = wallet->new_address(ask("Label for the new address (optional): ")); wallet->save(); }
        else { auto k = wallet->issued_keys(); for (auto it = k.rbegin(); it != k.rend(); ++it) if ((*it)->label != "(change)") { a = wallet->address_string((*it)->addr); break; } }
        std::cout << "\n";
        print_qr(a);
        std::cout << "\n  " << C_PURP << a << C_RESET << "\n\n";
    }

    void send(std::string to, std::string amount) {
        if (!open_wallet()) return;
        for (auto& c : wallet->contacts) if (c.name == to) to = c.address;
        if (to.empty()) to = ask("Pay to (address or contact name): ");
        for (auto& c : wallet->contacts) if (c.name == to) to = c.address;
        uint8_t v; Hash256 h;
        if (!decode_address(*p, to, v, h)) { std::cout << C_RED << "Invalid address." << C_RESET << "\n"; return; }
        if (amount.empty()) amount = ask("Amount in QNT: ");
        auto amt = parse_amount(amount);
        if (!amt || *amt < DUST_LIMIT) { std::cout << C_RED << "Invalid amount (minimum 0.000001, up to 10 decimals)." << C_RESET << "\n"; return; }
        if (!sync_wallet()) return;
        Transaction tx;
        Amount fee = 0;
        std::string err;
        if (!wallet->create_tx({{to, *amt}}, 0, lc->height(), tx, &fee, &err)) { std::cout << C_RED << err << C_RESET << "\n"; return; }
        std::cout << "\n  Send   " << C_BOLD << format_amount(*amt) << " QNT" << C_RESET << "\n  to     " << to
                  << "\n  fee    " << format_amount(fee) << " QNT (" << tx.full_size() << " bytes, Falcon-512 signed)\n\n";
        std::string ok = ask("Type yes to send: ");
        if (ok != "yes" && ok != "y") { std::cout << "Cancelled.\n"; return; }
        if (!lc->broadcast(tx, &err)) { std::cout << C_RED << "Rejected: " << err << C_RESET << "\n"; return; }
        wallet->tx_broadcast(tx, "");
        std::cout << C_GREEN << "Sent!" << C_RESET << " txid " << tx.txid().hex() << "\n";
    }

    void history() {
        if (!sync_wallet()) return;
        auto h = wallet->history();
        if (h.empty()) { std::cout << "No transactions yet.\n"; return; }
        for (auto& t : h) {
            int64_t conf = t.height < 0 ? 0 : lc->height() - t.height + 1;
            printf("  %s%-22s%s  %-8s  %s%s%s\n", t.delta < 0 ? C_RED : C_GREEN, (signed_amount(t.delta) + " QNT").c_str(), C_RESET,
                   t.coinbase ? "mined" : t.delta < 0 ? "sent" : "received", C_DIM,
                   conf ? (std::to_string(conf) + " conf").c_str() : "pending", C_RESET);
        }
    }

    void peers() {
        if (!online()) return;
        std::cout << "Connected nodes:\n";
        for (auto& s : lc->peer_names()) std::cout << "  " << s << "\n";
        std::cout << "Verified headers: " << lc->height() << "\n";
    }

    void seed() {
        if (!open_wallet()) return;
        if (ask("Show your 24 seed words? Anyone who sees them can take your QNT. Type show: ") != "show") return;
        std::cout << "\n  " << C_YEL << wallet->mnemonic() << C_RESET << "\n\n";
    }

    void contacts(const std::vector<std::string>& args) {
        if (!open_wallet()) return;
        if (args.size() >= 3 && args[0] == "add") {
            uint8_t v; Hash256 h;
            if (!decode_address(*p, args[2], v, h)) { std::cout << C_RED << "Invalid address." << C_RESET << "\n"; return; }
            wallet->contacts.push_back({args[1], args[2]});
            wallet->save();
            std::cout << "Saved " << args[1] << ". Pay with: qnt send " << args[1] << " <amount>\n";
            return;
        }
        if (wallet->contacts.empty()) std::cout << "Address book is empty. Add: qnt contacts add <name> <address>\n";
        for (auto& c : wallet->contacts) std::cout << "  " << c.name << "  " << C_DIM << c.address << C_RESET << "\n";
    }

    void menu() {
        for (;;) {
            std::cout << "\n" << C_PURP << C_BOLD << "  QUANT" << C_RESET << C_DIM << "  " << p->name << " phone wallet" << C_RESET << "\n"
                      << "  1) Balance        2) Receive        3) Send\n"
                      << "  4) History        5) New address    6) Contacts\n"
                      << "  7) Peers          8) Show seed      0) Quit\n";
            std::string c = ask("> ");
            if (!std::cin) return;
            if (c == "1") balance();
            else if (c == "2") receive(false);
            else if (c == "3") send("", "");
            else if (c == "4") history();
            else if (c == "5") receive(true);
            else if (c == "6") contacts({});
            else if (c == "7") peers();
            else if (c == "8") seed();
            else if (c == "0" || c == "q") return;
        }
    }
};

static void usage() {
    std::cout << "qnt - Quant phone wallet (post-quantum, light client)\n\n"
                 "  qnt                      interactive menu\n"
                 "  qnt new | restore        create a wallet / restore from 24 words\n"
                 "  qnt balance              sync and show balance\n"
                 "  qnt receive [new]        show address + QR code\n"
                 "  qnt send <addr|contact> <amount>\n"
                 "  qnt history | peers | seed\n"
                 "  qnt contacts [add <name> <address>]\n\n"
                 "Options: --testnet (default) --mainnet --regtest --addnode=IP:PORT --dir=PATH\n";
}

int main(int argc, char** argv) {
    log_set_stdout(false);
    App app;
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--testnet" || a == "-testnet") app.net = Network::Test;
        else if (a == "--mainnet" || a == "-mainnet") app.net = Network::Main;
        else if (a == "--regtest" || a == "-regtest") app.net = Network::Regtest;
        else if (a.rfind("--addnode=", 0) == 0) app.addnodes.push_back(a.substr(10));
        else if (a.rfind("--dir=", 0) == 0) app.dir = a.substr(6);
        else if (a == "-h" || a == "--help" || a == "help") { usage(); return 0; }
        else args.push_back(a);
    }
    app.p = &params_for(app.net);
    if (app.net == Network::Main && !app.p->launched) { std::cout << "Mainnet hasn't launched yet; using testnet.\n"; app.p = &params_for(Network::Test); }
    if (app.dir.empty()) {
        const char* home = getenv("HOME");
        app.dir = std::string(home ? home : ".") + "/.quant-light";
    }
    app.dir += "/" + app.p->name;
    std::string cmd = args.empty() ? "" : args[0];
    std::vector<std::string> rest(args.size() > 1 ? args.begin() + 1 : args.end(), args.end());
    if (cmd.empty()) {
        if (!Wallet::exists(app.wallet_path())) {
            std::cout << "Welcome to Quant!\n  1) Create a new wallet\n  2) Restore from seed words\n";
            std::string c = ask("> ");
            app.create(c == "2");
            if (!app.wallet) return 0;
        }
        app.menu();
    } else if (cmd == "new") app.create(false);
    else if (cmd == "restore") app.create(true);
    else if (cmd == "balance") app.balance();
    else if (cmd == "receive") app.receive(!rest.empty() && rest[0] == "new");
    else if (cmd == "send") app.send(rest.size() > 0 ? rest[0] : "", rest.size() > 1 ? rest[1] : "");
    else if (cmd == "history") app.history();
    else if (cmd == "peers") app.peers();
    else if (cmd == "seed") app.seed();
    else if (cmd == "contacts") app.contacts(rest);
    else { usage(); return 1; }
    return 0;
}
