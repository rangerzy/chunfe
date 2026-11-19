.pragma library

function background() {
    return "# 背景资料\n\n## 我的身份\n参与项目方案沟通与协作。\n\n## 业务资料\n正在讨论一项跨团队方案，需要明确目标、范围、负责人和交付时间。\n\n## 沟通偏好\n先澄清事实，再说明建议与取舍；信息不足时主动确认，不编造经历、数据或承诺。";
}

// A fixed conversation timeline, not a semantic classifier. Statements only transcribe.
function questions() {
    return [
        { kind: "question", question: "这次方案最需要先确认的是什么？",
          answer: "先确认目标、范围和验收方式，再讨论具体方案。\n\n可以这样回应：我想先确认这次要解决的核心问题、需要交付到什么程度，以及怎样判断方案达到了目标。把这三点对齐后，再讨论实现方式。\n\n如果信息还不完整，先列出待确认项，不替对方补充事实。",
          reply: "我想先确认目标、交付范围和验收标准。" },
        { kind: "statement", question: "我把刚才讨论的要点记下来了。", answer: "", reply: "" },
        { kind: "request", question: "请把下一步的安排和各自负责的事项整理一下。",
          answer: "把下一步拆成具体事项，并区分已确认和待确认的信息。\n\n可以这样回应：我们先列清交付项，再一起确认每项的负责人和时间点。还没有确定的部分先标为待确认，避免遗漏或误解。\n\n整理后复述一遍分工，确认双方理解一致。",
          reply: "我们先列清交付项，再一起确认负责人和时间。" },
        { kind: "objection", question: "这个时间可能来不及，而且现在的范围有点大。",
          answer: "先承认时间和范围上的顾虑，再讨论可以调整的部分。\n\n可以这样回应：理解你的担心。我们先核实时间约束，把必须完成和可以延后的内容分开，再比较缩小范围或分阶段交付的可行性。\n\n在工作量和资源还没核实前，不急着承诺新的日期。",
          reply: "我们先核实时间约束，再讨论缩小范围或分阶段交付。" },
        { kind: "statement", question: "我先把相关资料发到群里。", answer: "", reply: "" }
    ];
}

function records() {
    const samples = questions();
    return [
        { id: "sample-1", title: "项目方案沟通", date: "2026-11-02 14:30", duration: "24 分钟", model: "gpt-4.1", isDemo: true,
          questions: [samples[0], samples[2]].map(function(q) { return { kind:q.kind, question:q.question, answer:q.answer }; }),
          transcript: [{role:"other",text:samples[0].question,time:"00:12"},{role:"me",text:samples[0].reply,time:"00:19"},{role:"other",text:samples[1].question,time:"02:30"},{role:"other",text:samples[2].question,time:"04:26"},{role:"me",text:samples[2].reply,time:"04:35"}], backgroundText: background() },
        { id: "sample-2", title: "交付安排讨论", date: "2026-11-01 10:00", duration: "18 分钟", model: "gpt-4.1", isDemo: true,
          questions: [{kind:samples[3].kind,question:samples[3].question,answer:samples[3].answer}],
          transcript: [{role:"other",text:samples[3].question,time:"00:10"},{role:"me",text:samples[3].reply,time:"00:17"},{role:"other",text:samples[4].question,time:"00:26"}], backgroundText: "" }
    ];
}
