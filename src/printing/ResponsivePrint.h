#pragma once

#include <QEventLoop>
#include <QProgressDialog>
#include <QTimer>
#include <atomic>
#include <functional>
#include <exception>
#include <thread>

namespace occ {
// Cancellation requests a stop; it must never end the GUI event loop before
// the worker is finished. Driver calls cannot safely be forcibly interrupted.
inline void waitForPrintWorker(QProgressDialog &progress, const std::atomic<bool> &done,
                               std::atomic<bool> &cancel,
                               const std::function<void()> &update = {})
{
    QEventLoop loop;
    QTimer poll;
    int lastValue = progress.value();
    const auto requestStop = [&] {
        cancel = true;
        lastValue = progress.value();
        progress.setCancelButton(nullptr);
        progress.setValue(lastValue < 0 ? 0 : lastValue);
        progress.setLabelText(QObject::tr("Finishing the current card; no further cards will be sent..."));
        progress.show();
    };
    const auto connection = QObject::connect(&progress, &QProgressDialog::canceled,
                                             &loop, requestStop);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (update)
            update();
        if (done.load())
            loop.quit();
        else if (cancel.load() && !progress.isVisible())
            progress.show();
    });
    poll.start(50);
    progress.show();
    if (!done.load())
        loop.exec();
    QObject::disconnect(connection);
    progress.hide();
}

template <typename Work>
auto runResponsivePrint(QWidget *parent, Work work)
{
    using Result = decltype(work());
    Result result;
    std::exception_ptr error;
    std::atomic<bool> done{false}, cancel{false};
    QProgressDialog progress(QObject::tr("Sending test page; waiting for printer status..."),
                             QObject::tr("Stop after this card"), 0, 0, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    std::thread worker([&] {
        try { result = work(); }
        catch (...) { error = std::current_exception(); }
        done = true;
    });
    waitForPrintWorker(progress, done, cancel);
    worker.join();
    if (error)
        std::rethrow_exception(error);
    return result;
}
} // namespace occ