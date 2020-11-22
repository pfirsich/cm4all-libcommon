/*
 * Copyright 2007-2020 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "co/Cache.hxx"
#include "co/Task.hxx"
#include "co/InvokeTask.hxx"
#include "co/Sleep.hxx"
#include "event/Loop.hxx"

#include <gtest/gtest.h>

#include <cassert>

static unsigned n_started, n_finished;

struct ImmediateFactory {
	Co::Task<int> operator()(int key) noexcept {
		++n_started;
		++n_finished;
		co_return key;
	}
};

struct SleepFactory {
	EventLoop &event_loop;

	explicit SleepFactory(EventLoop &_event_loop) noexcept
		:event_loop(_event_loop) {}

	Co::Task<int> operator()(int key) noexcept {
		++n_started;
		co_await Co::Sleep(event_loop, std::chrono::milliseconds(1));
		++n_finished;
		co_return key;
	}
};

template<typename Factory>
using TestCache = Co::Cache<Factory, int, int, 2048, 2021>;

template<typename Factory>
struct Work {
	TestCache<Factory> &cache;

	Co::InvokeTask task;

	std::exception_ptr error;

	int value = -1;

	explicit Work(TestCache<Factory> &_cache) noexcept
		:cache(_cache) {}

	Co::InvokeTask Run(int key) noexcept {
		value = co_await cache.Get(key);
	}

	void Start(int key) {
		task = Run(key);
	}

	void OnCompletion(std::exception_ptr _error) noexcept {
		assert(value >= 0 || _error);

		error = std::move(_error);
	}
};

TEST(CoCache, Cached)
{
	EventLoop event_loop;

	using Factory = ImmediateFactory;
	using Cache = TestCache<Factory>;

	Cache cache;

	n_started = n_finished = 0;

	Work w1(cache), w2(cache);
	w1.Start(42);
	w2.Start(42);

	event_loop.Dispatch();

	ASSERT_EQ(w1.value, 42);
	ASSERT_EQ(w2.value, 42);
	ASSERT_EQ(n_started, 1u);
	ASSERT_EQ(n_finished, 1u);
}

TEST(CoCache, Sleep)
{
	using Factory = SleepFactory;
	using Cache = TestCache<Factory>;

	EventLoop event_loop;
	Cache cache(event_loop);

	n_started = n_finished = 0;

	Work w1(cache), w2(cache), w3(cache), w4(cache);
	w1.Start(42);
	w2.Start(3);
	w3.Start(42);

	ASSERT_EQ(n_started, 2u);
	ASSERT_EQ(n_finished, 0u);

	event_loop.Dispatch();

	ASSERT_EQ(w1.value, 42);
	ASSERT_EQ(w2.value, 3);
	ASSERT_EQ(w3.value, 42);
	ASSERT_EQ(n_started, 2u);
	ASSERT_EQ(n_finished, 2u);

	w4.Start(42);

	event_loop.Dispatch();

	ASSERT_EQ(w4.value, 42);
	ASSERT_EQ(n_started, 2u);
	ASSERT_EQ(n_finished, 2u);
}

TEST(CoCache, CancelSingle)
{
	using Factory = SleepFactory;
	using Cache = TestCache<Factory>;

	EventLoop event_loop;
	Cache cache(event_loop);

	n_started = n_finished = 0;

	{
		Work w(cache);
		w.Start(42);
	}

	event_loop.Dispatch();

	ASSERT_EQ(n_started, 1u);
	ASSERT_EQ(n_finished, 0u);
}

TEST(CoCache, CancelOne)
{
	using Factory = SleepFactory;
	using Cache = TestCache<Factory>;

	EventLoop event_loop;
	Cache cache(event_loop);

	n_started = n_finished = 0;

	Work w1(cache);
	w1.Start(42);

	{
		Work w2(cache);
		w2.Start(42);
	}

	ASSERT_EQ(n_started, 1u);
	ASSERT_EQ(n_finished, 0u);

	event_loop.Dispatch();

	ASSERT_EQ(w1.value, 42);
	ASSERT_EQ(n_started, 1u);
	ASSERT_EQ(n_finished, 1u);
}

TEST(CoCache, CancelBothSingle)
{
	using Factory = SleepFactory;
	using Cache = TestCache<Factory>;

	EventLoop event_loop;
	Cache cache(event_loop);

	n_started = n_finished = 0;

	{
		Work w(cache);
		w.Start(42);
	}

	{
		Work w(cache);
		w.Start(42);
	}

	ASSERT_EQ(n_started, 2u);
	ASSERT_EQ(n_finished, 0u);

	event_loop.Dispatch();

	ASSERT_EQ(n_started, 2u);
	ASSERT_EQ(n_finished, 0u);
}

TEST(CoCache, CancelAll)
{
	using Factory = SleepFactory;
	using Cache = TestCache<Factory>;

	EventLoop event_loop;
	Cache cache(event_loop);

	n_started = n_finished = 0;

	{
		Work w1(cache), w2(cache);
		w1.Start(42);
		w2.Start(42);
	}

	event_loop.Dispatch();

	ASSERT_EQ(n_started, 1u);
	ASSERT_EQ(n_finished, 0u);
}
