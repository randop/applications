// Exam App Main JS

const QUESTIONS_MD = 'questions.md';

async function fetchQuestions() {
  const res = await fetch(QUESTIONS_MD);
  const text = await res.text();
  return parseQuestions(text);
}

function parseQuestions(md) {
  // Split by horizontal rule
  const blocks = md.split(/^-{3,}$/m).map(b => b.trim()).filter(Boolean);
  const questions = blocks.map(block => {
    // Get question (h3)
    const qMatch = block.match(/^###\s+(.+)/m);
    const question = qMatch ? qMatch[1].trim() : '';
    // Get choices (ordered list)
    const choices = [];
    const choiceRegex = /^\d+\.\s+(.+)/gm;
    let m;
    while ((m = choiceRegex.exec(block))) {
      choices.push(m[1].trim());
    }
    // Get answer (number in blockquote)
    const aMatch = block.match(/^>\s*(\d+)/m);
    const answer = aMatch ? parseInt(aMatch[1], 10) - 1 : -1;
    return { question, choices, answer };
  });
  return questions;
}

function fisherYatesShuffle(arr) {
  const a = arr.slice();
  for (let i = a.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [a[i], a[j]] = [a[j], a[i]];
  }
  return a;
}

function createExamApp(questions) {
  // For each question, randomize choices using Fisher-Yates and update answer index
  const randomizedQuestions = questions.map(q => {
    const indices = fisherYatesShuffle([...Array(q.choices.length).keys()]);
    const shuffledChoices = indices.map(i => q.choices[i]);
    const newAnswer = indices.indexOf(q.answer);
    return {
      question: q.question,
      choices: shuffledChoices,
      answer: newAnswer
    };
  });

  let current = 0;
  let order = fisherYatesShuffle([...Array(randomizedQuestions.length).keys()]);
  let correct = 0;
  let incorrect = 0;
  let answered = 0;

  const root = document.getElementById('root');
  root.innerHTML = '';

  function renderQuestion(idx) {
    const q = randomizedQuestions[order[idx]];
    root.innerHTML = `
      <div class="question">
        <h2>Question ${answered + 1} of ${randomizedQuestions.length}</h2>
        <div style="margin: 18px 0 12px 0; font-size: 1.15rem;">${q.question}</div>
        <form id="choices">
          ${q.choices.map((c, i) => `
            <div style="margin-bottom: 10px;">
              <label style="cursor:pointer;">
                <input type="radio" name="choice" value="${i}"> ${c}
              </label>
            </div>
          `).join('')}
        </form>
        <div class="message" id="message"></div>
        <div class="score" id="score"></div>
      </div>
    `;
    renderButton('Submit', handleSubmit, false);
    updateScore();
  }

  function renderButton(text, onClick, disabled) {
    let btn = document.getElementById('exam-btn');
    if (!btn) {
      btn = document.createElement('button');
      btn.className = 'bottom-right';
      btn.id = 'exam-btn';
      document.body.appendChild(btn);
    }
    btn.textContent = text;
    btn.disabled = !!disabled;
    btn.onclick = onClick;
  }

  function handleSubmit() {
    const form = document.getElementById('choices');
    const selected = form.querySelector('input[name="choice"]:checked');
    const msg = document.getElementById('message');
    if (!selected) {
      msg.textContent = 'Please select an answer.';
      return;
    }
    const q = randomizedQuestions[order[current]];
    const ans = parseInt(selected.value, 10);
    if (ans === q.answer) {
      msg.textContent = 'Correct!';
      msg.style.color = '#388e3c';
      correct++;
    } else {
      msg.textContent = `Incorrect. The correct answer is: ${q.choices[q.answer]}`;
      msg.style.color = '#d32f2f';
      incorrect++;
    }
    answered++;
    // Disable all choices
    Array.from(form.elements).forEach(el => el.disabled = true);
    renderButton(current < randomizedQuestions.length - 1 ? 'Next' : 'Finish', handleNext, false);
    updateScore();
  }

  function handleNext() {
    current++;
    if (current < randomizedQuestions.length) {
      renderQuestion(current);
    } else {
      renderResult();
      document.getElementById('exam-btn').remove();
    }
  }

  function updateScore() {
    const score = document.getElementById('score');
    if (score) {
      score.innerHTML = `Score: <b>${correct}</b> correct, <b>${incorrect}</b> incorrect (${((correct / (answered||1)) * 100).toFixed(1)}%)`;
    }
  }

  function renderResult() {
    root.innerHTML = `
      <div class="result">
        <h2>Exam Complete!</h2>
        <div class="score">You answered <b>${correct}</b> out of <b>${questions.length}</b> correctly.</div>
        <div class="score">Correct: <b>${correct}</b></div>
        <div class="score">Incorrect: <b>${incorrect}</b></div>
        <div class="score">Percentage: <b>${((correct / questions.length) * 100).toFixed(1)}%</b></div>
      </div>
    `;
  }

  renderQuestion(current);
}

window.addEventListener('DOMContentLoaded', async () => {
  try {
    const questions = await fetchQuestions();
    if (!questions.length) throw new Error('No questions found.');
    createExamApp(questions);
  } catch (e) {
    document.getElementById('root').innerHTML = `<div style="color:#d32f2f;">Failed to load questions: ${e.message}</div>`;
  }
}); 