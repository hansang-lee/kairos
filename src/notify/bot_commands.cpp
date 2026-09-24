#include "notify/bot_commands.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace notify {

namespace {

std::string won(double v) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(0) << v;
    std::string s = os.str();
    // Thousands separators: a nine-digit balance is unreadable without them, and
    // this is being read on a phone.
    const bool neg = !s.empty() && s.front() == '-';
    if (neg) {
        s.erase(s.begin());
    }
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(s.size()) - 3; i > 0; i -= 3) {
        s.insert(static_cast<std::size_t>(i), ",");
    }
    return (neg ? "-" : "") + s;
}

std::string pct(double v) {
    std::ostringstream os;
    os << std::showpos << std::fixed << std::setprecision(2) << v << "%";
    return os.str();
}

}  // namespace

bool isAuthorised(const std::string& senderChatId, const std::string& configuredChatId) {
    // An empty configured chat means the bot is not set up; answering anyone then
    // would be worse than answering nobody.
    return !configuredChatId.empty() && senderChatId == configuredChatId;
}

std::string commandOf(const std::string& text) {
    std::string word;
    for (const char c : text) {
        if (c == ' ' || c == '\n' || c == '\t') {
            break;
        }
        word.push_back(c);
    }
    // "/status@kairos_bot" is what Telegram sends in a group; the suffix is noise.
    const auto at = word.find('@');
    if (at != std::string::npos) {
        word.erase(at);
    }
    std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return std::tolower(c); });
    return word;
}

std::string formatStatus(const BotSnapshot& snap) {
    if (!snap.ok) {
        return "계좌 조회 실패: " + snap.message;
    }

    double stockValue = 0.0;
    for (const auto& h : snap.holdings) {
        stockValue += h.evalAmount;
    }
    const double total    = snap.totalEval > 0.0 ? snap.totalEval : snap.cashBalance;
    const double stockPct = total > 0.0 ? stockValue / total * 100.0 : 0.0;
    const double ret = snap.initialCapital > 0.0 ? (total - snap.initialCapital) / snap.initialCapital * 100.0 : 0.0;

    std::ostringstream os;
    os << "평가금액 " << won(total) << "원\n"
       << "수익률 " << pct(ret) << "  (원금 " << won(snap.initialCapital) << "원)\n"
       << "주식 " << won(stockValue) << "원 (" << std::fixed << std::setprecision(1) << stockPct << "%)\n"
       << "현금 " << won(snap.cashBalance) << "원 (" << std::fixed << std::setprecision(1) << (100.0 - stockPct)
       << "%)\n"
       << "보유 " << snap.holdings.size() << "종목";
    return os.str();
}

std::string formatPositions(const BotSnapshot& snap) {
    if (!snap.ok) {
        return "계좌 조회 실패: " + snap.message;
    }
    if (snap.holdings.empty()) {
        return "보유 종목 없음";
    }

    std::ostringstream os;
    for (std::size_t i = 0; i < snap.holdings.size(); ++i) {
        const auto&  h    = snap.holdings[i];
        const double cost = h.avgPrice * static_cast<double>(h.quantity);
        const double pl   = cost > 0.0 ? (h.evalAmount - cost) / cost * 100.0 : 0.0;
        if (i > 0) {
            os << "\n";
        }
        os << h.name << " (" << h.ticker << ")\n"
           << "  " << h.quantity << "주  평단 " << won(h.avgPrice) << "\n"
           << "  평가 " << won(h.evalAmount) << "원  " << pct(pl);
    }
    return os.str();
}

std::string formatSignals(const std::vector<BotSignal>& signals) {
    if (signals.empty()) {
        return "활성 전략 없음";
    }
    std::ostringstream os;
    for (std::size_t i = 0; i < signals.size(); ++i) {
        const auto& s = signals[i];
        if (i > 0) {
            os << "\n";
        }
        os << "#" << s.profileId << " " << s.ticker << "  " << s.signal << "  " << won(s.price) << "원";
        if (s.heldQty > 0) {
            os << "  (보유 " << s.heldQty << "주)";
        }
    }
    return os.str();
}

std::string formatTrades(const std::vector<BotTrade>& trades, std::size_t limit) {
    if (trades.empty()) {
        return "거래 기록 없음";
    }
    // Newest first: the last thing that happened is what somebody checking their
    // phone wants to see, not the first.
    const std::size_t  take = std::min(limit, trades.size());
    std::ostringstream os;
    for (std::size_t i = 0; i < take; ++i) {
        const auto& t = trades[trades.size() - 1 - i];
        if (i > 0) {
            os << "\n";
        }
        os << t.time << "  " << t.event << "  " << t.ticker << " " << t.side << " " << t.quantity << "주 @ "
           << won(t.price);
        if (!t.reason.empty()) {
            os << "  " << t.reason;
        }
    }
    return os.str();
}

std::string helpText() {
    return "kairos\n"
           "/status  평가금액·수익률·주식현금 비율\n"
           "/positions  보유 종목별 손익\n"
           "/signals  현재 전략 신호\n"
           "/trades  최근 주문·체결\n"
           "/help  이 목록";
}

}  // namespace notify
