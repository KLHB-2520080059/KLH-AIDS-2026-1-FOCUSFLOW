// Loads the course outcomes from the backend and shows them on the landing page.
document.addEventListener('DOMContentLoaded', async () => {
  const grid = document.getElementById('syllabus-grid');
  if (!grid) return;

  try {
    const { outcomes } = await api('/syllabus');
    outcomes.forEach(co => {
      grid.append(h('article', { class: 'syllabus-card' + (co.project ? ' used' : '') }, [
        h('span', { class: 'co', text: co.id }),
        h('h3', {}, [
          co.title,
          co.project ? h('span', { class: 'tag', text: 'Used in this project' }) : null
        ]),
        h('ul', {}, co.topics.map(topic => h('li', { text: topic })))
      ]));
    });
  } catch (error) {
    grid.append(h('p', { class: 'muted', text: 'Could not load the syllabus. Make sure the server is running.' }));
  }
});
