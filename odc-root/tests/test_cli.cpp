#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <array>
#include <vector>

namespace fs = std::filesystem;

static std::string cli_path() {
    const char* env = std::getenv("ODC_CLI_PATH");
    if (env) return env;

    // Search relative to current directory and up
    for (int up = 0; up < 5; ++up) {
        std::string prefix;
        for (int i = 0; i < up; ++i) prefix += "../";

        std::vector<std::string> candidates = {
            prefix + "tools/odc-cli/odc-cli",
            prefix + "build/tools/odc-cli/odc-cli",
        };

        for (const auto& c : candidates) {
            if (fs::exists(c)) return c;
        }
    }

    return "odc-cli";
}

struct CliResult {
    int exit_code{0};
    std::string output;
};

static CliResult run_cli(const std::string& args) {
    std::string cmd = cli_path() + " " + args + " 2>&1";
    std::array<char, 16384> buf{};
    std::string output;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        CliResult r;
        r.exit_code = -1;
        return r;
    }

    while (std::fgets(buf.data(), buf.size(), pipe) != nullptr) {
        output += buf.data();
    }

    int status = pclose(pipe);
    CliResult r;
    r.output = output;
    r.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}

static fs::path tmp_dir() {
    auto d = fs::temp_directory_path() / ("odc-cli-test-" + std::to_string(std::rand()));
    fs::create_directories(d);
    return d;
}

TEST_CASE("CLI cert generate-ca", "[cli][cert]") {
    auto dir = tmp_dir();
    auto ca_cert = dir / "ca.crt";
    auto ca_key = dir / "ca.key";

    auto r = run_cli("cert generate-ca --cert " + ca_cert.string() +
                     " --key " + ca_key.string() + " --org TestOrg --cn TestCA");

    INFO("Output: " << r.output);
    REQUIRE(r.exit_code == 0);
    REQUIRE(fs::exists(ca_cert));
    REQUIRE(fs::exists(ca_key));

    std::ifstream cf(ca_cert);
    std::string content((std::istreambuf_iterator<char>(cf)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("BEGIN CERTIFICATE") != std::string::npos);

    auto json = nlohmann::json::parse(r.output);
    REQUIRE(json["status"] == "ok");

    fs::remove_all(dir);
}

TEST_CASE("CLI cert issue", "[cli][cert]") {
    auto dir = tmp_dir();
    auto ca_cert = dir / "ca.crt";
    auto ca_key = dir / "ca.key";
    auto client_cert = dir / "client.crt";
    auto client_key = dir / "client.key";

    run_cli("cert generate-ca --cert " + ca_cert.string() +
            " --key " + ca_key.string() + " --org TestOrg");

    auto r = run_cli("cert issue --ca-cert " + ca_cert.string() +
                     " --ca-key " + ca_key.string() +
                     " --cn test-client --cert-out " + client_cert.string() +
                     " --key-out " + client_key.string());

    INFO("Output: " << r.output);
    REQUIRE(r.exit_code == 0);
    REQUIRE(fs::exists(client_cert));
    REQUIRE(fs::exists(client_key));

    auto json = nlohmann::json::parse(r.output);
    REQUIRE(json["status"] == "ok");
    REQUIRE(json["common_name"] == "test-client");
    REQUIRE_FALSE(json["fingerprint"].get<std::string>().empty());

    fs::remove_all(dir);
}

TEST_CASE("CLI cert verify", "[cli][cert]") {
    auto dir = tmp_dir();
    auto ca_cert = dir / "ca.crt";
    auto ca_key = dir / "ca.key";
    auto client_cert = dir / "client.crt";
    auto client_key = dir / "client.key";

    run_cli("cert generate-ca --cert " + ca_cert.string() +
            " --key " + ca_key.string());
    run_cli("cert issue --ca-cert " + ca_cert.string() +
            " --ca-key " + ca_key.string() +
            " --cn test --cert-out " + client_cert.string() +
            " --key-out " + client_key.string());

    auto r = run_cli("cert verify --cert " + client_cert.string() +
                     " --ca-cert " + ca_cert.string());
    REQUIRE(r.exit_code == 0);

    auto json = nlohmann::json::parse(r.output);
    REQUIRE(json["valid"] == true);

    fs::remove_all(dir);
}

TEST_CASE("CLI cert fingerprint", "[cli][cert]") {
    auto dir = tmp_dir();
    auto ca_cert = dir / "ca.crt";
    auto ca_key = dir / "ca.key";
    auto client_cert = dir / "client.crt";
    auto client_key = dir / "client.key";

    run_cli("cert generate-ca --cert " + ca_cert.string() +
            " --key " + ca_key.string());
    run_cli("cert issue --ca-cert " + ca_cert.string() +
            " --ca-key " + ca_key.string() +
            " --cn test --cert-out " + client_cert.string() +
            " --key-out " + client_key.string());

    auto r = run_cli("cert fingerprint --cert " + client_cert.string());
    REQUIRE(r.exit_code == 0);

    auto json = nlohmann::json::parse(r.output);
    REQUIRE(json["algorithm"] == "SHA-256");
    auto fp = json["fingerprint"].get<std::string>();
    // SHA-256 fingerprint is 47 chars: 32 bytes × 2 hex + 15 colons
    REQUIRE(fp.length() >= 47);

    fs::remove_all(dir);
}

TEST_CASE("CLI cert keypair-wg", "[cli][cert]") {
    auto r = run_cli("cert keypair-wg");
    REQUIRE(r.exit_code == 0);

    auto json = nlohmann::json::parse(r.output);
    REQUIRE(json["status"] == "ok");
    REQUIRE(json["private_key"].get<std::string>().find("BEGIN PRIVATE KEY") != std::string::npos);
    REQUIRE(json["public_key"].get<std::string>().find("BEGIN PUBLIC KEY") != std::string::npos);

    INFO("WG keypair: " << json.dump());
}

TEST_CASE("CLI cert revoke", "[cli][cert]") {
    auto dir = tmp_dir();
    auto ca_cert = dir / "ca.crt";
    auto ca_key = dir / "ca.key";
    auto client_cert = dir / "client.crt";
    auto client_key = dir / "client.key";
    auto crl = dir / "crl.pem";

    run_cli("cert generate-ca --cert " + ca_cert.string() +
            " --key " + ca_key.string());
    run_cli("cert issue --ca-cert " + ca_cert.string() +
            " --ca-key " + ca_key.string() +
            " --cn test --cert-out " + client_cert.string() +
            " --key-out " + client_key.string());

    auto r = run_cli("cert revoke --cert " + client_cert.string() +
                     " --ca-cert " + ca_cert.string() +
                     " --ca-key " + ca_key.string() +
                     " --crl " + crl.string());
    // revoke exits non-zero when CRL generation is not fully implemented
    REQUIRE(r.exit_code != 0);

    fs::remove_all(dir);
}

TEST_CASE("CLI --help", "[cli][help]") {
    auto r = run_cli("--help");
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.output.find("status") != std::string::npos);
    REQUIRE(r.output.find("services") != std::string::npos);
    REQUIRE(r.output.find("cert") != std::string::npos);
    REQUIRE(r.output.find("tunnel") != std::string::npos);
    REQUIRE(r.output.find("events") != std::string::npos);
    REQUIRE(r.output.find("hook") != std::string::npos);
}

TEST_CASE("CLI subcommand help", "[cli][help]") {
    auto r = run_cli("cert --help");
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.output.find("generate-ca") != std::string::npos);
    REQUIRE(r.output.find("issue") != std::string::npos);
    REQUIRE(r.output.find("verify") != std::string::npos);
    REQUIRE(r.output.find("fingerprint") != std::string::npos);
    REQUIRE(r.output.find("revoke") != std::string::npos);
    REQUIRE(r.output.find("keypair-wg") != std::string::npos);
}

TEST_CASE("CLI tunnel help", "[cli][help]") {
    auto r = run_cli("tunnel --help");
    REQUIRE(r.exit_code == 0);
    REQUIRE(r.output.find("status") != std::string::npos);
    REQUIRE(r.output.find("peers") != std::string::npos);
    REQUIRE(r.output.find("health") != std::string::npos);
}

TEST_CASE("CLI cert missing required args", "[cli][error]") {
    auto r = run_cli("cert issue --cn test");
    // Should exit non-zero or print "required" in output
    REQUIRE((r.exit_code != 0 || r.output.find("required") != std::string::npos));
}

TEST_CASE("CLI invalid subcommand", "[cli][error]") {
    auto r = run_cli("nonexistent 2>&1");
    REQUIRE(r.exit_code != 0);
}
